#include <iostream>
#include <bitset>
#include <bit>
#include <fstream>
#include <filesystem>
#include <vector>
#include <string>
#include <string_view>
#include <span>

namespace {
enum RegisterWord : uint8_t {
  kRegAX = 0b000,
  kRegCX = 0b001,
  kRegDX = 0b010,
  kRegBX = 0b011,
  kRegSP = 0b100,
  kRegBP = 0b101,
  kRegSI = 0b110,
  kRegDI = 0b111
};

enum RegisterByte : uint8_t {
  kRegAL = 0b000,
  kRegCL = 0b001,
  kRegDL = 0b010,
  kRegBL = 0b011,
  kRegAH = 0b100,
  kRegCH = 0b101,
  kRegDH = 0b110,
  kRegBH = 0b111
};

std::string_view ToString(RegisterWord reg) {
  switch (reg) {
    case kRegAX: return "ax";
    case kRegCX: return "cx";
    case kRegDX: return "dx";
    case kRegBX: return "bx";
    case kRegSP: return "sp";
    case kRegBP: return "bp";
    case kRegSI: return "si";
    case kRegDI: return "di";
  }
  return {};
}

std::string_view ToString(RegisterByte reg) {
  switch (reg) {
    case kRegAL: return "al";
    case kRegCL: return "cl";
    case kRegDL: return "dl";
    case kRegBL: return "bl";
    case kRegAH: return "ah";
    case kRegCH: return "ch";
    case kRegDH: return "dh";
    case kRegBH: return "bh";
  }
  return {};
}

enum Opcode : uint8_t {
  kMov_RegisterMemory_ToFrom_Register = 0b100010'00 >> 2
};

struct MovReg2Reg {
  enum RegType : uint8_t {
    REG_BYTE,
    REG_WORD
  } w;

  union {
    struct {
      RegisterWord src, dst;
    } reg_word;

    struct {
      RegisterByte src, dst;
    } reg_byte;
  };

  std::string Generate() {
    std::string result = "mov ";
    if (w == REG_WORD) {
      result += ToString(reg_word.dst);
      result += ',';
      result += ToString(reg_word.src);
    } else {
      result += ToString(reg_byte.dst);
      result += ',';
      result += ToString(reg_byte.src);
    }
    return result;
  }
};

std::vector<std::string_view> ProcessCliArgs(int argc, char** argv) {
  if (argc < 2) {
    const auto error_message =
        std::string("Usage: ") + argv[0] + " <file_1> <file_2> ...";
    throw std::runtime_error(error_message);
  }

  std::vector<std::string_view> input_files{};

  for (int i = 1; i < argc; ++i) {
    const auto file = argv[i];
    if (std::filesystem::is_regular_file(file)) {
      input_files.push_back(file);
    } else {
      std::cout << "Ignoring " << file << " as it's a non-regular file\n";
    }
  }
  return input_files;
}

std::vector<uint8_t> ReadFile(std::string_view file) {
  const auto size = std::filesystem::file_size(file);
  if (size == 0) {
    // no way
    return {};
  }
  std::vector<uint8_t> result(size);
  std::ifstream stream(file.data(), std::ios::binary);
  stream.read(reinterpret_cast<char*>(result.data()), result.size());
  return result;
}

std::vector<std::string> DecodeInstructions(
    std::span<const uint8_t> instructions) {
  auto it = instructions.begin();
  std::vector<std::string> result{};

  while (it != instructions.end()) {
    auto byte = *it;
    const uint8_t opcode = (byte & 0b111111'00) >> 2;
    switch (opcode) {
      case Opcode::kMov_RegisterMemory_ToFrom_Register: {
        const auto d = (byte & 0b000000'10) >> 1;
        const auto w = byte & 0b000000'01;
        if (++it == instructions.end()) {
          std::cout << "Instruction " << std::bitset<8>(opcode)
                    << " is malformed\n";
          return {};
        }
        byte = *it;
        const uint8_t mod = (byte & 0b11'000'000) >> 6;
        const uint8_t reg = (byte & 0b00'111'000) >> 3;
        const uint8_t rm = (byte & 0b00'000'111);
        std::cout << "Instruction layout: " << std::bitset<6>(opcode)
                  << std::bitset<1>(d) << std::bitset<1>(w) << "|"
                  << std::bitset<2>(mod) << std::bitset<3>(reg)
                  << std::bitset<3>(rm) << std::endl;

        switch (mod) {
          case 0b11: {
            MovReg2Reg instr{};
            instr.w = static_cast<MovReg2Reg::RegType>(w);
            if (d == 0) {
              instr.reg_byte.src = static_cast<RegisterByte>(reg);
              instr.reg_byte.dst = static_cast<RegisterByte>(rm);
            } else {
              instr.reg_byte.src = static_cast<RegisterByte>(rm);
              instr.reg_byte.dst = static_cast<RegisterByte>(reg);
            }
            const auto decoded = instr.Generate();
            result.push_back(decoded);
          } break;
          default: {
            std::cout << "Unsupported mode: " << std::bitset<2>(mod)
                      << std::endl;
            return {};
          } break;
        }
        ++it;
      } break;
      default: {
        std::cout << "Unsupported instruction: " << std::bitset<8>(opcode)
                  << std::endl;
        return {};
      } break;
    }
  }

  return result;
}

void WriteFile(std::string_view file_name, std::span<const std::string> data) {
  const std::string out_name = std::string(file_name) + "my.asm";
  std::ofstream stream(out_name);
  stream << "bits 16\n";
  for (const auto& line : data) {
    stream << line << '\n';
  }
}
}  // namespace

int main(int argc, char** argv) try {
  const auto input_files = ProcessCliArgs(argc, argv);
  for (const auto& file : input_files) {
    const auto instructions = ReadFile(file);
    if (instructions.empty()) {
      std::cout << "Ignoring file " << file
                << " as it contains no instructions\n";
      continue;
    }
    const auto decoded_instructions = DecodeInstructions(instructions);
    WriteFile(file, decoded_instructions);
  }
  return 0;
} catch (const std::exception& e) {
  std::cerr << e.what() << std::endl;
}