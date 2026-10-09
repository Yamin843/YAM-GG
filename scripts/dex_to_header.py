#!/usr/bin/env python3
import sys

def main():
    if len(sys.argv) < 3:
        print("Usage: dex_to_header.py <input.dex> <output.h>")
        sys.exit(1)

    input_path = sys.argv[1]
    output_path = sys.argv[2]

    with open(input_path, 'rb') as f:
        data = f.read()

    with open(output_path, 'w') as f:
        f.write("// Auto-generated. Do not edit.\n")
        f.write("#ifndef YAMGG_EMBEDDED_DEX_H\n")
        f.write("#define YAMGG_EMBEDDED_DEX_H\n\n")
        f.write("#include <cstddef>\n\n")
        f.write("namespace yamgg {\n\n")
        f.write("static const unsigned int kDexSize = %d;\n\n" % len(data))
        f.write("static const unsigned char kDexBytes[] = {\n")

        line = "    "
        for i, b in enumerate(data):
            line += "0x%02x," % b
            if (i + 1) % 16 == 0:
                f.write(line + "\n")
                line = "    "
            else:
                line += " "
        if line.strip():
            f.write(line + "\n")

        f.write("};\n\n")
        f.write("} // namespace yamgg\n\n")
        f.write("#endif // YAMGG_EMBEDDED_DEX_H\n")

if __name__ == '__main__':
    main()
