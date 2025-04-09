#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <string.h>
#include <jpeglib.h>
#include <setjmp.h>

#define DEBUG 0
#define OUTPUT_PGM 1
#define OUTPUT_JPEG 1

typedef struct {
    unsigned char *data;
    int width;
    int height;
} Image;

void save_jpeg(const char *filename, Image *img, int quality) {
    struct jpeg_compress_struct cinfo;
    struct jpeg_error_mgr jerr;
    FILE *outfile;
    JSAMPROW row_pointer[1];

    cinfo.err = jpeg_std_error(&jerr);
    jpeg_create_compress(&cinfo);

    if ((outfile = fopen(filename, "wb")) == NULL) {
        fprintf(stderr, "Can't open %s for writing\n", filename);
        return;
    }
    jpeg_stdio_dest(&cinfo, outfile);

    cinfo.image_width = img->width;
    cinfo.image_height = img->height;
    cinfo.input_components = 1;
    cinfo.in_color_space = JCS_GRAYSCALE;

    jpeg_set_defaults(&cinfo);
    jpeg_set_quality(&cinfo, quality, TRUE);
    jpeg_start_compress(&cinfo, TRUE);

    while (cinfo.next_scanline < cinfo.image_height) {
        row_pointer[0] = &img->data[cinfo.next_scanline * img->width];
        jpeg_write_scanlines(&cinfo, row_pointer, 1);
    }

    jpeg_finish_compress(&cinfo);
    fclose(outfile);
    jpeg_destroy_compress(&cinfo);
}

void save_pgm(const char *filename, Image *img) {
    FILE *fp = fopen(filename, "wb");
    if (!fp) {
        fprintf(stderr, "Can't open %s for writing\n", filename);
        return;
    }
    fprintf(fp, "P5\n%d %d\n255\n", img->width, img->height);
    fwrite(img->data, 1, img->width * img->height, fp);
    fclose(fp);
}

Image* create_image(int width, int height) {
    Image *img = (Image*)malloc(sizeof(Image));
    img->width = width;
    img->height = height;
    img->data = (unsigned char*)malloc(width * height);
    return img;
}

void free_image(Image *img) {
    if (img) {
        free(img->data);
        free(img);
    }
}

/*
 * Extract the DC terms from the specified component ci.
 */
Image* extract_dc(j_decompress_ptr cinfo, jvirt_barray_ptr *coeffs, int ci) {
    jpeg_component_info *ci_ptr = &cinfo->comp_info[ci];
    Image *dc = create_image(ci_ptr->width_in_blocks, ci_ptr->height_in_blocks);
    assert(dc != NULL);

    JQUANT_TBL *tbl = ci_ptr->quant_table;
    UINT16 dc_quant = tbl->quantval[0];

#if DEBUG
    printf("DCT method: %x\n", cinfo->dct_method);
    printf("component: %d (%d x %d blocks) sampling: (%d x %d)\n", 
           ci, ci_ptr->width_in_blocks, ci_ptr->height_in_blocks,
           ci_ptr->h_samp_factor, ci_ptr->v_samp_factor);

    printf("quantization table: %d\n", ci);
    for (int i = 0; i < DCTSIZE2; ++i) {
        printf("% 4d ", (int)(tbl->quantval[i]));
        if ((i + 1) % 8 == 0) printf("\n");
    }
    printf("raw DC coefficients:\n");
#endif

    JBLOCKARRAY buf = (cinfo->mem->access_virt_barray)((j_common_ptr)cinfo, coeffs[ci], 0, 
                                                      ci_ptr->v_samp_factor, FALSE);
    for (int sf = 0; (JDIMENSION)sf < ci_ptr->height_in_blocks; ++sf) {
        for (JDIMENSION b = 0; b < ci_ptr->width_in_blocks; ++b) {
            int intensity = buf[sf][b][0] * dc_quant / DCTSIZE + 128;
            intensity = (intensity > 0) ? intensity : 0;
            intensity = (intensity < 255) ? intensity : 255;
            dc->data[sf * dc->width + b] = (unsigned char)intensity;

#if DEBUG
            printf("% 2d ", buf[sf][b][0]);                        
#endif
        }
#if DEBUG
        printf("\n");
#endif
    }

    return dc;
}

Image* upscale_chroma(Image *quarter, int full_width, int full_height) {
    Image *full = create_image(full_width, full_height);
    
    // Simple nearest-neighbor upscaling
    for (int y = 0; y < full_height; y++) {
        int src_y = y * quarter->height / full_height;
        for (int x = 0; x < full_width; x++) {
            int src_x = x * quarter->width / full_width;
            full->data[y * full_width + x] = quarter->data[src_y * quarter->width + src_x];
        }
    }
    
    return full;
}

int read_JPEG_file(char *filename) {
    struct jpeg_decompress_struct cinfo;
    struct jpeg_error_mgr jerr;
    FILE *infile;

    if ((infile = fopen(filename, "rb")) == NULL) {
        fprintf(stderr, "can't open %s\n", filename);
        return 0;
    }

    cinfo.err = jpeg_std_error(&jerr);
    jpeg_create_decompress(&cinfo);
    jpeg_stdio_src(&cinfo, infile);
    jpeg_read_header(&cinfo, TRUE);
    
    jvirt_barray_ptr *coeffs = jpeg_read_coefficients(&cinfo);

    Image *y = extract_dc(&cinfo, coeffs, 0);
    Image *cb_q = extract_dc(&cinfo, coeffs, 1);
    Image *cr_q = extract_dc(&cinfo, coeffs, 2);

    Image *cb = upscale_chroma(cb_q, y->width, y->height);
    Image *cr = upscale_chroma(cr_q, y->width, y->height);

#if OUTPUT_PGM
    save_pgm("y.pgm", y);
    save_pgm("cb.pgm", cb);
    save_pgm("cr.pgm", cr);
#endif

#if OUTPUT_JPEG
    save_jpeg("y_dc.jpg", y, 90);
    save_jpeg("cb_dc.jpg", cb, 90);
    save_jpeg("cr_dc.jpg", cr, 90);
#endif

    free_image(y);
    free_image(cb_q);
    free_image(cr_q);
    free_image(cb);
    free_image(cr);

    jpeg_finish_decompress(&cinfo);
    jpeg_destroy_decompress(&cinfo);
    fclose(infile);

    return 1;
}

int main(int argc, char **argv) {
    if (argc != 2) {
        fprintf(stderr, "usage: %s filename.jpg\n", argv[0]);
        return 1;
    }

    if (!read_JPEG_file(argv[1])) {
        return 1;
    }

    printf("DC components saved as:\n");
    printf("  - y_dc.jpg (Luminance)\n");
    printf("  - cb_dc.jpg (Chroma Blue)\n");
    printf("  - cr_dc.jpg (Chroma Red)\n");

    return 0;
}