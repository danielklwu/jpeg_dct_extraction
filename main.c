#include <stdio.h>
#include <stdlib.h>
#include <jpeglib.h>
#include <jerror.h>

// zigzag pattern array
static int zigzag[64] = {
     0,  1,  8, 16,  9,  2,  3, 10,
    17, 24, 32, 25, 18, 11,  4,  5,
    12, 19, 26, 33, 40, 48, 41, 34,
    27, 20, 13,  6,  7, 14, 21, 28,
    35, 42, 49, 56, 57, 50, 43, 36,
    29, 22, 15, 23, 30, 37, 44, 51,
    58, 59, 52, 45, 38, 31, 39, 46,
    53, 60, 61, 54, 47, 55, 62, 63
};

int main(int argc, char **argv) {
    if (argc != 3) {
        fprintf(stderr, "Usage: %s <input.jpg> <output.txt>\n", argv[0]);
        return 1;
    }

    const char *input_filename = argv[1];
    const char *output_filename = argv[2];

    FILE *infile = fopen(input_filename, "rb");
    if (!infile) {
        fprintf(stderr, "Error opening input file %s\n", input_filename);
        return 1;
    }

    struct jpeg_decompress_struct cinfo;
    struct jpeg_error_mgr jerr;

    cinfo.err = jpeg_std_error(&jerr);
    jpeg_create_decompress(&cinfo); // init a JPEG decompression object
    jpeg_stdio_src(&cinfo, infile); // tells cinfo where to read the JPEG data from
    if (jpeg_read_header(&cinfo, TRUE) != JPEG_HEADER_OK) {
        fprintf(stderr, "Not a valid JPEG file.\n");
        jpeg_destroy_decompress(&cinfo);
        fclose(infile);
        return 1;
    }

    printf("JPEG is %s\n", (cinfo.progressive_mode ? "Progressive" : "Baseline")); // jpg type

    jvirt_barray_ptr *coeffs = jpeg_read_coefficients(&cinfo); // read DCT coeff
    if (!coeffs) {
        fprintf(stderr, "Failed to read JPEG coefficients.\n");
        jpeg_abort_decompress(&cinfo);
        fclose(infile);
        return 1;
    }

    FILE *outfile = fopen(output_filename, "w");
    if (!outfile) {
        fprintf(stderr, "Error opening output file %s\n", output_filename);
        jpeg_finish_decompress(&cinfo);
        jpeg_destroy_decompress(&cinfo);
        fclose(infile);
        return 1;
    }

    int null_blocks = 0; // count # of NULL blocks

    for (int compno = 0; compno < cinfo.num_components; compno++) { // usually Y, Cb, Cr
        jpeg_component_info *comp = &cinfo.comp_info[compno];
        fprintf(outfile, "Component %d: %s (%dx%d blocks)\n",
           compno,
           (compno == 0 ? "Y (Luma)" : (compno == 1 ? "Cb (Chroma Blue)" : "Cr (Chroma Red)")),
           comp->width_in_blocks,
           comp->height_in_blocks);
        for (JDIMENSION blk_y = 0; blk_y < comp->height_in_blocks; blk_y++) { 
            // get row
            JBLOCKARRAY row = cinfo.mem->access_virt_barray((j_common_ptr)&cinfo, coeffs[compno], blk_y, 1, FALSE);
            if (!row) {
                fprintf(stderr, "Failed to access block row at blk_y=%u\n", blk_y);
                continue; 
            }

            for (JDIMENSION blk_x = 0; blk_x < comp->width_in_blocks; blk_x++) { 
                // get col
                JBLOCKROW block = row[blk_x];
                if (!block) {
                    // fprintf(stderr, "NULL block at blk_y=%u, blk_x=%u\n", blk_y, blk_x);
                    null_blocks++;
                    continue;
                }
                
                for (int i = 0; i < 64; i++) { // print block in zigzag order
                    // fprintf(outfile, "%d ", block[i]);
                    fprintf(outfile, "%d ", block[zigzag[i]]);
                }
                fprintf(outfile, "\n");
            }
        }
    }
    
    printf("There are %d NULL blocks found in %s\n", null_blocks, input_filename);

    fclose(outfile);
    jpeg_finish_decompress(&cinfo);
    jpeg_destroy_decompress(&cinfo);
    fclose(infile);

    return 0;
}