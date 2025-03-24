#include <iostream>
#include <bitset>
#include <bit>
#include <fstream>
#include <filesystem>
#include <vector>
#include <string>
#include <string_view>
#include <format>
#include <span>

namespace {
enum Register : uint8_t {
  kRegAL = 0b0000,
  kRegCL = 0b0001,
  kRegDL = 0b0010,
  kRegBL = 0b0011,
  kRegAH = 0b0100,
  kRegCH = 0b0101,
  kRegDH = 0b0110,
  kRegBH = 0b0111,
  kRegAX = 0b1000,
  kRegCX = 0b1001,
  kRegDX = 0b1010,
  kRegBX = 0b1011,
  kRegSP = 0b1100,
  kRegBP = 0b1101,
  kRegSI = 0b1110,
  kRegDI = 0b1111
};

std::string_view ToString(Register reg) {
  static constexpr std::string_view kStringRepr[] = {
      "al", "cl", "dl", "bl", "ah", "ch", "dh", "bh",
      "ax", "cx", "dx", "bx", "sp", "bp", "si", "di"};
  return kStringRepr[uint8_t(reg)];
}

enum Opcode : uint8_t {
  kMov_RegisterMemory_ToFrom_Register = 0b100010,
  kMov_Immediate_To_Register = 0b1011
};

struct BytePack {
  uint8_t hi, lo;

  uint16_t Pack() const noexcept { return uint16_t(hi) << 8 | lo; }
};

struct MovImmediateToReg {
  uint8_t byte_1;
  BytePack disp;

  std::string Generate() const {
    constexpr auto kFormatString = "mov {}, {}";
    const auto options = byte_1 & 0b0000'1111;
    const auto reg = ToString(Register(options));

    if (options < 0b1000) {
      return std::format(kFormatString, reg, static_cast<int8_t>(disp.lo));
    }
    return std::format(kFormatString, reg, static_cast<int16_t>(disp.Pack()));
  }
};

struct MovMemoryMod {
  uint8_t w;
  uint8_t d;

  uint8_t mod;

  union {
    struct {
      uint8_t rm;
      Register reg;
    };
    uint8_t operands[2] = {};
  };

  BytePack disp;

  std::string GenerateEffectiveAddress() const {
    std::string effective_address_str{};
    switch (rm) {
      case 0b000: {
        effective_address_str = "[bx + si";
      } break;
      case 0b001: {
        effective_address_str = "[bx + di";
      } break;
      case 0b010: {
        effective_address_str = "[bp + si";
      } break;
      case 0b011: {
        effective_address_str = "[bp + di";
      } break;
      case 0b100: {
        effective_address_str = "[si";
      } break;
      case 0b101: {
        effective_address_str = "[di";
      } break;
      case 0b110: {
        if (mod == 0b00) {
          return std::format("[{:x}]", disp.Pack());
        } else {
          effective_address_str = "[bp";
        }
      } break;
      case 0b111: {
        effective_address_str = "[bx";
      } break;
      default: {
        const auto error = std::format(
            "Instruction 'mov' is malformed: invalid r/m field {:b}.", rm);
        throw std::runtime_error(error);
      } break;
    }

    const auto packed_disp = disp.Pack();
    if (packed_disp != 0) {
      if (mod == 0b01) {
        effective_address_str += std::format(" + {}", disp.lo);
      } else if (mod == 0b10) {
        effective_address_str += std::format(" + {}", packed_disp);
      }
    }
    effective_address_str += "]";
    return effective_address_str;
  }

  std::string GenerateReg2Reg() const {
    constexpr auto kFormatString = "mov {}, {}";
    const auto i = uint8_t(d != 0);
    const auto j = uint8_t(d == 0);
    const auto w_correct = w * 0b1000;
    const auto op_1 = ToString(Register(operands[i] + w_correct));
    const auto op_2 = ToString(Register(operands[j] + w_correct));
    return std::format(kFormatString, op_1, op_2);
  }

  std::string Generate() const {
    constexpr auto kFormatString = "mov {}, {}";
    switch (mod) {
      case 0b11: {
        return GenerateReg2Reg();
      } break;
      case 0b00:
      case 0b01:
      case 0b10: {
        const auto address = GenerateEffectiveAddress();
        const auto w_correct = w * 0b1000;
        const auto r = ToString(Register(operands[1] + w_correct));
        if (d == 1) {
          return std::format(kFormatString, r, address);
        }
        return std::format(kFormatString, address, r);
      } break;
      default: {
      } break;
    }
    throw std::runtime_error(std::format("Unsupported mov mod: {:b}", mod));
  }
};

std::string_view ProcessCliArgs(int argc, char** argv) {
  if (argc != 2) {
    throw std::runtime_error(std::format("Usage: {} <file>", argv[0]));
  }

  if (!std::filesystem::is_regular_file(argv[1])) {
    throw std::runtime_error(std::format("{} is not a regular file", argv[1]));
  }
  return argv[1];
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

class InstructionStreamIterator {
  std::span<const uint8_t> instruction_stream_;
  std::span<const uint8_t>::const_iterator it_;

 public:
  InstructionStreamIterator(std::span<const uint8_t> stream)
      : instruction_stream_(stream), it_(instruction_stream_.cbegin()) {}

  operator bool() const noexcept { return it_ != instruction_stream_.end(); }

  InstructionStreamIterator& operator++() {
    ++it_;
    return *this;
  }

  uint8_t operator*() const { return *it_; }

  uint8_t ExpectNext(Opcode opcode) {
    ++it_;
    if (!*this) {
      throw std::runtime_error(std::format(
          "Instruction {:b} is malformed: unexpected end of stream",
          uint8_t(opcode)));
    }
    return this->operator*();
  }
};

std::vector<std::string> DecodeInstructions(
    std::span<const uint8_t> instruction_stream) {
  InstructionStreamIterator it(instruction_stream);
  std::vector<std::string> result{};

  while (it) {
    const auto byte_1 = *it;
    const auto opcode_prefix = (byte_1 & 0b1111'0000) >> 4;
    const auto opcode_suffix = byte_1 & 0b0000'1111;

    switch (opcode_prefix) {
      case 0b1000: {
        switch (opcode_suffix) {
          case 0b1000:
          case 0b1001:
          case 0b1010:
          case 0b1011:
          case 0b1100:
          case 0b1110: {
            const auto opcode = Opcode((byte_1 & 0b111111'00) >> 2);
            const auto byte_2 = it.ExpectNext(opcode);

            const auto d = (byte_1 & 0b000000'10) >> 1;
            const auto w = byte_1 & 0b000000'01;
            const uint8_t mod = (byte_2 & 0b11'000'000) >> 6;
            const uint8_t reg = (byte_2 & 0b00'111'000) >> 3;
            const uint8_t rm = (byte_2 & 0b00'000'111);

            MovMemoryMod instr{};
            instr.d = d;
            instr.w = w;
            instr.rm = rm;
            instr.mod = mod;
            instr.reg = static_cast<Register>(reg);

            if (mod == 0b01 || mod == 0b10) {
              instr.disp.lo = it.ExpectNext(opcode);
            }
            if (mod == 0b10 || mod == 0b00 && rm == 0b110) {
              instr.disp.hi = it.ExpectNext(opcode);
            }

            const auto decoded = instr.Generate();
            result.push_back(decoded);
            ++it;
          } break;
          default: break;
        }
      } break;
      case 0b1011: {
        const auto opcode = Opcode((byte_1 & 0b1111'0000) >> 4);
        MovImmediateToReg instr{};
        instr.byte_1 = byte_1;
        instr.disp.lo = it.ExpectNext(opcode);
        if ((byte_1 & 0b0000'1111) >= 0b1000) {
          instr.disp.hi = it.ExpectNext(opcode);
        }
        const auto decoded = instr.Generate();
        result.push_back(decoded);
        ++it;
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
  const auto file = ProcessCliArgs(argc, argv);
  const auto instructions = ReadFile(file);
  if (instructions.empty()) {
    throw std::runtime_error("File is empty");
  }
  const auto decoded_instructions = DecodeInstructions(instructions);
  if (decoded_instructions.empty()) {
    throw std::runtime_error("File contains no instructions");
  }
  WriteFile(file, decoded_instructions);
  return 0;
} catch (const std::exception& e) {
  std::cerr << e.what() << std::endl;
}