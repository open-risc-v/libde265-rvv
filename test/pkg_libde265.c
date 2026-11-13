/* libde265 探针：解码 test/data/tiny-64x64.265（x265 生成的 10 帧 64x64），
   逐帧哈希 Y/U/V 平面。语料随仓库固定（sha256 5a7a4a8f…），保证可复现。 */
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <libde265/de265.h>

static unsigned long long st = 0xcbf29ce484222325ULL;
static void fnv_add(const void *buf, size_t n)
{
	const unsigned char *p = (const unsigned char *)buf;
	for (size_t i = 0; i < n; i++) { st ^= p[i]; st *= 0x100000001b3ULL; }
}

int main(int argc, char **argv)
{
	if (argc < 3) { fprintf(stderr, "usage: %s <in.265> <out.bin>\n", argv[0]); return 2; }
	FILE *in = fopen(argv[1], "rb");
	if (!in) { perror("fopen in"); return 2; }
	unsigned char *buf = malloc(1 << 20);
	if (!buf) return 2;
	size_t len = fread(buf, 1, 1 << 20, in);
	fclose(in);

	de265_decoder_context *ctx = de265_new_decoder();
	de265_start_worker_threads(ctx, 0);
	de265_push_data(ctx, buf, (int)len, 0, NULL);
	de265_flush_data(ctx);
	free(buf);

	FILE *out = fopen(argv[2], "wb");
	if (!out) { perror("fopen out"); return 2; }

	int frames = 0, more = 1, err = 0;
	unsigned long long bytes = 0;
	while (more) {
		err = de265_decode(ctx, &more);
		if (err == DE265_OK) {
			const de265_image *img;
			while ((img = de265_get_next_picture(ctx)) != NULL) {
				for (int c = 0; c < 3; c++) {
					int w = de265_get_image_width(img, c);
					int h = de265_get_image_height(img, c);
					/* stride 由 de265_get_image_plane 的第三个出参回填（API 里没有独立的 stride 函数） */
					int stride = 0;
					const uint8_t *p = de265_get_image_plane(img, c, &stride);
					for (int y = 0; y < h; y++) {
						fwrite(p + (size_t)y * stride, 1, (size_t)w, out);
						fnv_add(p + (size_t)y * stride, (size_t)w);
						bytes += (unsigned long long)w;
					}
				}
				frames++;
			}
		} else break;
		if (!more) break;
	}
	fclose(out);
	de265_free_decoder(ctx);

	printf("dec frames=%d bytes=%llu fnv_px=%016llx\n", frames, bytes, st);
	return (frames == 10 && err == DE265_OK) ? 0 : 1;
}
