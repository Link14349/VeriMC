#include "verimc/ast.hpp"
#include "verimc/compiler.hpp"
#include "verimc/vmclJson.hpp"
#include <fstream>
#include <iostream>
#include <nlohmann/json.hpp>
#include <random>
#include <set>
#include <sstream>

namespace {
using Json = nlohmann::json;
std::string read(const std::filesystem::path& path) {
    std::error_code error;
    auto size = std::filesystem::file_size(path, error);
    if (error || size > 256 * 1024 * 1024)
        throw std::runtime_error("Cannot read file or file exceeds 256 MB");
    std::ifstream in(path, std::ios::binary);
    std::ostringstream data;
    data << in.rdbuf();
    if (!in)
        throw std::runtime_error("Cannot read file");
    return data.str();
}
void usage() {
    std::cout
        << "VeriMC 0.1.0\n"
           "  verimcCli compile input.vmc --top Module [-o output.vmcl] [--param name=value] [--root path]\n"
           "    [--max-nodes N] [--max-bits N] [--max-instances N] [--max-steps N] [--max-depth N]\n"
           "  verimcCli parse input.vmc\n"
           "  verimcCli validate input.vmcl\n";
}
} // namespace
int main(int argc, char** argv) {
    try {
        if (argc == 1 || (argc == 2 && std::string(argv[1]) == "--help")) {
            usage();
            return 0;
        }
        if (argc < 3)
            throw std::runtime_error("Expected command and input file");
        std::string command = argv[1];
        std::filesystem::path input = std::filesystem::absolute(argv[2]);
        if (command == "parse") {
            if (argc != 3)
                throw std::runtime_error("Unexpected parse arguments");
            auto text = read(input);
            if (text.size() > 4 * 1024 * 1024)
                throw std::runtime_error("Source exceeds 4 MB");
            auto source = verimc::parseSource(input.filename().string(), text);
            std::cout << "Parsed " << source.declarations.size() << " declarations\n";
            return 0;
        }
        if (command == "validate") {
            if (argc != 3)
                throw std::runtime_error("Unexpected validate arguments");
            verimc::readVmclJson(read(input));
            std::cout << "Valid logical graph\n";
            return 0;
        }
        if (command != "compile")
            throw std::runtime_error("Unknown command: " + command);
        verimc::CompileOptions options;
        options.input = input;
        options.projectRoot = input.parent_path();
        std::filesystem::path output = input;
        output.replace_extension(".vmcl");
        bool force = false;
        auto integer = [](const std::string& value) {
            if (value.empty() || value.find_first_not_of("0123456789") != std::string::npos)
                throw std::runtime_error("Expected positive integer budget");
            auto n = std::stoull(value);
            if (n == 0)
                throw std::runtime_error("Budget must be positive");
            return static_cast<std::size_t>(n);
        };
        for (int i = 3; i < argc; ++i) {
            std::string flag = argv[i];
            if (flag == "--force") {
                force = true;
                continue;
            }
            if (i + 1 == argc)
                throw std::runtime_error("Missing value for " + flag);
            std::string value = argv[++i];
            if (flag == "--top")
                options.top = value;
            else if (flag == "-o" || flag == "--output")
                output = value;
            else if (flag == "--root")
                options.projectRoot = value;
            else if (flag == "--param") {
                auto split = value.find('=');
                if (split == std::string::npos ||
                    !options.parameters.emplace(value.substr(0, split), value.substr(split + 1)).second)
                    throw std::runtime_error("Invalid or duplicate parameter");
            } else if (flag == "--max-nodes")
                options.maxNodes = integer(value);
            else if (flag == "--max-bits")
                options.maxBits = integer(value);
            else if (flag == "--max-instances")
                options.maxInstances = integer(value);
            else if (flag == "--max-steps")
                options.maxSteps = integer(value);
            else if (flag == "--max-depth")
                options.maxDepth = integer(value);
            else
                throw std::runtime_error("Unknown option: " + flag);
        }
        if (options.top.empty())
            throw std::runtime_error("Select a top module with --top");
        if (output.extension() != ".vmcl")
            throw std::runtime_error("Output extension must be .vmcl");
        if (std::filesystem::exists(output) && !force)
            throw std::runtime_error("Output exists; use --force to replace it");
        auto graph = verimc::compileFile(options);
        auto bytes = verimc::writeVmclJson(graph);
        // A sibling temporary directory keeps writes on the same filesystem.
        // A failed compile/write never truncates the previous successful artifact.
        auto parent = std::filesystem::absolute(output).parent_path();
        std::random_device random;
        std::filesystem::path temporary;
        for (int attempt = 0; attempt < 16; ++attempt) {
            auto candidate = parent / (".verimcWrite" + std::to_string(random()));
            if (std::filesystem::create_directory(candidate)) {
                temporary = candidate;
                break;
            }
        }
        if (temporary.empty())
            throw std::runtime_error("Cannot reserve temporary output");
        struct Cleanup {
            std::filesystem::path path;
            ~Cleanup() {
                std::error_code error;
                std::filesystem::remove_all(path, error);
            }
        } cleanup{temporary};
        auto staged = temporary / "result.vmcl";
        std::ofstream out(staged, std::ios::binary);
        out.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
        out.close();
        if (!out)
            throw std::runtime_error("Cannot write .vmcl output");
        if (force)
            std::filesystem::rename(staged, output);
        else
            std::filesystem::create_hard_link(
                staged, output); // Exclusive publication, even if another writer raced us.
        std::cout << output.string() << ": " << graph.nodes.size() << " nodes, " << graph.connections.size()
                  << " connections\n";
        return 0;
    } catch (const verimc::Diagnostic& e) {
        std::cerr << verimc::writeDiagnosticJson(e) << '\n';
        return 1;
    } catch (const std::exception& e) {
        std::cerr << Json({{"code", "ECompiler"}, {"message", e.what()}}).dump() << '\n';
        return 1;
    }
}
