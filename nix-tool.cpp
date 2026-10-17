#include <iostream>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>
#include <chrono>
#include <ctime>
#include <iomanip>
#include <unistd.h>
#include <sys/wait.h>
#include <limits.h>
#include <sys/stat.h>

#include <iostream>
#include <string>
#include <cstdlib> // Para getenv

// Função para descobrir o caminho do NixOS dinamicamente em tempo de execução
std::string get_flake_path() {
    // 1. Se o usuário definiu uma variável de ambiente customizada, usa ela
    const char* custom_path = std::getenv("NIX_FLAKE_PATH");
    if (custom_path != nullptr) {
        return std::string(custom_path);
    }

    // 2. Senão, pega o HOME atual do usuário e aponta para a pasta Nixos
    const char* home = std::getenv("HOME");
    if (home != nullptr) {
        return std::string(home) + "/Nixos";
    }

    // 3. Fallback final seguro
    return "./Nixos";
}

// Função segura para executar comandos sem invocar o shell (/bin/sh)
int run_command(const std::vector<std::string>& args) {
    pid_t pid = fork();
    if (pid < 0) {
        return -1;
    } else if (pid == 0) {
        std::vector<char*> c_args;
        for (const auto& arg : args) {
            c_args.push_back(const_cast<char*>(arg.c_str()));
        }
        c_args.push_back(nullptr);

        execvp(c_args[0], c_args.data());
        _exit(127);
    } else {
        int status;
        waitpid(pid, &status, 0);
        if (WIFEXITED(status)) {
            return WEXITSTATUS(status);
        }
        return -1;
    }
}

// Retorna o timestamp atual [YYYY-MM-DD HH:MM:SS]
std::string get_current_timestamp() {
    auto now = std::chrono::system_clock::now();
    auto in_time_t = std::chrono::system_clock::to_time_t(now);
    std::stringstream ss;
    ss << std::put_time(std::localtime(&in_time_t), "%Y-%m-%d %H:%M:%S");
    return "[" + ss.str() + "]";
}

// Obtém o caminho do symlink do sistema atual
std::string get_current_system_path() {
    char buf[PATH_MAX];
    ssize_t len = readlink("/run/current-system", buf, sizeof(buf) - 1);
    if (len != -1) {
        buf[len] = '\0';
        return std::string(buf);
    }
    return "/nix/store/unknown";
}

// Obtém o número da geração atual do perfil do sistema
std::string get_current_generation() {
    char buf[PATH_MAX];
    ssize_t len = readlink("/nix/var/nix/profiles/system", buf, sizeof(buf) - 1);
    if (len != -1) {
        buf[len] = '\0';
        std::string target(buf);
        size_t dash = target.find_last_of('-');
        size_t prev_dash = target.find_last_of('-', dash - 1);
        if (prev_dash != std::string::npos && dash != std::string::npos) {
            return target.substr(prev_dash + 1, dash - prev_dash - 1);
        }
    }
    return "unknown";
}

// Grava no log
void append_log(const std::string& system_path, const std::string& commit_msg, const std::string& status) {
    std::ofstream log_file(HIST_FILE, std::ios::app);
    if (log_file.is_open()) {
        log_file << get_current_timestamp() << " " 
                 << system_path << ": " 
                 << commit_msg << " | status: " 
                 << status << "\n";
    }
}

// 1. Comando Rebuild
void cmd_rebuild(const std::string& flake_path) {
    std::cout << "==> [1/4] Atualizando o Git...\n";
    if (run_command({"git", "-C", flake_path, "add", "."}) != 0) {
        std::cerr << "Erro ao rodar 'git add'. Abortando.\n";
        append_log("N/A", "Git Add Failed", "error");
        return;
    }

    std::cout << "==> [2/4] Executando rebuild...\n";
    int rebuild_res = run_command({"sudo", "nixos-rebuild", "switch", "--flake", flake_path + "#nixos"});
    if (rebuild_res != 0) {
        std::cerr << "==> Rebuild falhou com código " << rebuild_res << "\n";
        append_log("N/A", "NixOS Rebuild Failed", "error");
        return;
    }

    std::string gen = get_current_generation();
    std::string sys_path = get_current_system_path();
    std::string commit_msg = "NixOS Generation " + gen;

    std::cout << "==> [3/4] Realizando o commit...\n";
    int commit_res = run_command({"git", "-C", flake_path, "commit", "-m", commit_msg});
    std::string status_str = (commit_res == 0) ? "ok" : "warn";

    std::cout << "==> [4/4] Escrevendo no "<<HIST_FILE<<"...\n";
    append_log(sys_path, commit_msg, status_str);

    std::cout << "==> Sucesso!\n";
}

void cmd_rollback(int argc, char* argv[]) {
    if (argc > 2) {
        std::string gen = argv[2];
        std::string profile_path = "/nix/var/nix/profiles/system-" + gen + "-link/bin/switch-to-configuration";
        
        std::cout << "==> Revertendo para a geração: " << gen << "...\n";
        int res = run_command({"sudo", profile_path, "switch"});
        if (res != 0) {
            std::cerr << "Erro no Rollback " << gen << ".\n";
        } else {
            std::cout << "==> Rollback sucedido " << gen << "!\n";
        }
    } else {
        std::cout << "==> Revertendo para a geração anterior.\n";
        run_command({"sudo", "nixos-rebuild", "switch", "--rollback"});
    }
}

void cmd_history() {
    std::ifstream log_file(HIST_FILE);
    if (!log_file.is_open()) {
        std::cout << HIST_FILE <<" vazio ou ausente.\n";
        return;
    }
    std::string line;
    std::cout << "=== Histórico de Builds ===\n";
    while (std::getline(log_file, line)) {
        std::cout << line << "\n";
    }
}

// 4. Comando GC (Garbage Collector)
void cmd_gc() {
    std::cout << "==> Removendo gerações antigas...\n";
    run_command({"sudo", "nix-collect-garbage", "-d"});
}

// 5. Comando New-Shell
void cmd_new_shell() {
    std::ofstream outfile("shell.nix");
    if (!outfile.is_open()) {
        std::cerr << "Erro ao criar o arquivo shell.nix.\n";
        return;
    }

    outfile << "{\n"
            << "  pkgs ? import <nixpkgs> {} ?\n\n"
            << "pkgs.mkShell {\n"
            << "  packages = with pkgs; [\n"
            << "    gcc\n"
            << "    g++\n"
            << "    gnumake\n"
            << "  ];\n"
            << "}\n";
    
    outfile.close();
    std::cout << "==> Arquivo 'shell.nix' criado!\n";
}

// 6. Comando New-Flake
void cmd_new_flake() {
    std::ofstream outfile("flake.nix");
    if (!outfile.is_open()) {
        std::cerr << "Erro ao criar o arquivo flake.nix.\n";
        return;
    }

    outfile << "{\n"
            << "  description = \" you should edit ur flake.nix.\";\n\n"
            << "  inputs = {\n"
            << "    nixpkgs.url = \"github:NixOS/nixpkgs/nixos-unstable\";\n"
            << "  };\n\n"
            << "  outputs = { self, nixpkgs }:\n"
            << "    let\n"
            << "      system = \"x86_64-linux\";\n"
            << "      pkgs = nixpkgs.legacyPackages.${system};\n"
            << "    in\n"
            << "    {\n"
            << "        packages.${system}.default = pkgs.stdenv.mkDerivation {\n"
            << "        pname = \"meu-projeto\";\n"
            << "        version = \"0.1.0\";\n"
            << "        src = ./.;\n\n"
            << "        nativeBuildInputs = with pkgs; [\n"
            << "          gcc\n"
            << "          gnumake\n"
            << "        ];\n\n"
            << "        buildPhase = ''\n"
            << "          g++ -O3 main.cpp -o meu-projeto\n"
            << "        '';\n\n"
            << "        installPhase = ''\n"
            << "          mkdir -p $out/bin\n"
            << "          cp meu-projeto $out/bin/\n"
            << "        '';\n"
            << "      };\n\n"
            << "      devShells.${system}.default = pkgs.mkShell {\n"
            << "        packages = with pkgs; [\n"
            << "          gcc\n"
            << "          g++\n"
            << "          gnumake\n"
            << "        ];\n"
            << "      };\n"
            << "    };\n"
            << "}\n";

    outfile.close();
    std::cout << "==> Arquivo 'flake.nix' criado\n";
}

// 7. Comando Build
void cmd_build() {
    std::cout << "==> Executando build...\n";
    int res = run_command({"nix", "build"});
    if (res != 0) {
        std::cerr << "Erro ao executar nix build.\n";
    } else {
        std::cout << "==> Build concluído com sucesso!\n";
        std::cout << "==> (Abra ./result)\n";
    }
}

void print_help() {
    std::cout << "Uso: nix-tool <comando> [opções]\n\n"
              << "Comandos disponíveis:\n"
              << "  rebuild            Atualiza o sistema\n"
              << "  rollback [gen]     Faz um rollback\n"
              << "  history            Exibe o "<<HIST_FILE<< " formatado\n"
              << "  gc                 Garbage collector\n"
              << "  new-shell          Gera um arquivo shell.nix padrão\n"
              << "  new-flake          Gera um arquivo flake.nix padrão\n"
              << "  build              Executa build no ./flake.nix \n";
}
bool is_nixos() {
    struct stat info;
    if (stat("/etc/nixos", &info) == 0 && S_ISDIR(info.st_mode)) {
        return true;
    }

    std::ifstream os_release("/etc/os-release");
    if (os_release.is_open()) {
        std::string line;
        while (std::getline(os_release, line)) {
            if (line.find("ID=nixos") != std::string::npos) {
                return true;
            }
        }
    }
    return false;
}
int main(int argc, char* argv[]) {
    if (!is_nixos){
        std::cerr<<"Não foi possivel detectar se é NixOS\n";
        std::cerr<<"Crie a pasta /etc/nixos para contornar esse erro.\n";
        
    }
    if (argc < 2) {
        print_help();
        return 1;
    }

    std::string command = argv[1];
    std::string flake_path = get_flake_path();

    if (command == "rebuild") {
        cmd_rebuild(flake_path);
    } else if (command == "rollback") {
        cmd_rollback(argc, argv);
    } else if (command == "history") {
        cmd_history();
    } else if (command == "gc") {
        cmd_gc();
    } else if (command == "new-shell") {
        cmd_new_shell();
    } else if (command == "new-flake") {
        cmd_new_flake();
    } else if (command == "build") {
        cmd_build();
    } else {
        std::cout << "Comando desconhecido: " << command << "\n\n";
        print_help();
        return 1;
    }

    return 0;
}
