/* 整链解码基准：同一进程内重复解码 10 帧 64x64，去掉进程启动开销。
   用法：dec_bench <in.265> <reps>   → 打印 total_us 与每帧 us */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <libde265/de265.h>

static long long now_us(void) {
  struct timespec ts; clock_gettime(CLOCK_MONOTONIC, &ts);
  return (long long)ts.tv_sec * 1000000LL + ts.tv_nsec / 1000;
}

int main(int argc, char **argv) {
  if (argc < 3) { fprintf(stderr, "usage: %s <in.265> <reps>\n", argv[0]); return 2; }
  int reps = atoi(argv[2]);
  FILE *in = fopen(argv[1], "rb");
  if (!in) { perror("fopen"); return 2; }
  static unsigned char buf[1 << 20];
  size_t len = fread(buf, 1, sizeof buf, in);
  fclose(in);
  int frames = 0;
  long long t0 = now_us();
  for (int k = 0; k < reps; k++) {
    de265_decoder_context *ctx = de265_new_decoder();
    de265_start_worker_threads(ctx, 0);
    de265_push_data(ctx, buf, (int)len, 0, NULL);
    de265_flush_data(ctx);
    const de265_image *img = NULL;
    int more = 1;
    while (more) {
      de265_error err = de265_decode(ctx, &more);
      if (err != DE265_OK && err != DE265_ERROR_WAITING_FOR_INPUT_DATA) break;
      while ((img = de265_get_next_picture(ctx)) != NULL) frames++;
      if (!more) break;
    }
    de265_free_decoder(ctx);
  }
  long long dt = now_us() - t0;
  printf("frames=%d reps=%d total_us=%lld per_frame_us=%.1f\n", frames, reps, dt, (double)dt / frames);
  return 0;
}
