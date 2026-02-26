import sys


def convert_psf_to_header(input_file, output_file):
    with open(input_file, "rb") as f:
        data = f.read()

    # PSF1 Header is 4 bytes. Data starts at index 4.
    with open(output_file, "w") as f:
        f.write("unsigned char font_psf[] = {\n    ")
        for i, byte in enumerate(data):
            f.write(f"0x{byte:02x}, ")
            if (i + 1) % 12 == 0:
                f.write("\n    ")
        f.write("\n};\n")


if __name__ == "__main__":
    # Usage: python3 script.py myfont.psf font_psf.h
    convert_psf_to_header(sys.argv[1], sys.argv[2])
