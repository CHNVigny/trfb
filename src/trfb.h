#ifndef TRFB_H_INC
#define TRFB_H_INC

#include <stdint.h>
#include <stdlib.h>
#include <time.h>
#include <c11threads.h>
#include <sys/socket.h>

/* Tiny RFB (VNC) server implementation */

#ifdef __cplusplus
extern "C" {
#endif /* } */

#define TRFB_EOF    0xffff
#define TRFB_BUFSIZ 2048
typedef struct trfb_io {
	void *ctx;
	int error;
	/* read/write functions must not touch buf, pos and len! */
	unsigned char rbuf[TRFB_BUFSIZ];
	size_t rlen, rpos;
	unsigned char wbuf[TRFB_BUFSIZ];
	size_t wlen;

	void (*free)(void *ctx);
	/* This I/O functions must process I/O operation and return:
	 *   count of bytes processed on success
	 *   0 on timeout
	 *   -1 on error (and closed connection)
	 */
	ssize_t (*read)(struct trfb_io *io, void *buf, ssize_t len, unsigned timeout);
	ssize_t (*write)(struct trfb_io *io, const void *buf, ssize_t len, unsigned timeout);
} trfb_io_t;

typedef enum trfb_protocol {
	trfb_v3 = 3,
	trfb_v7 = 7,
	trfb_v8 = 8
} trfb_protocol_t;

typedef enum trfb_auth {
	trfb_auth_none
} trfb_auth_t;

typedef struct trfb_format {
	unsigned char bpp, depth;
	unsigned char big_endian;
	unsigned char true_color;
	uint16_t rmax, gmax, bmax;
	unsigned char rshift, gshift, bshift;
} trfb_format_t;

typedef struct trfb_framebuffer {
	mtx_t lock;

	unsigned width, height;
	/**
	 * Bytes per pixel. Supported values are:
	 *   1 - and in this case this is 256-color image.
	 *   2 - pixels is uint16_t*
	 *   4 - pixels is uint32_t*
	 */
	unsigned char bpp;

	uint32_t rmask, gmask, bmask;
	unsigned char rshift, gshift, bshift;
	unsigned char rnorm, gnorm, bnorm;

	/* To get actual pixel you need to:
	 * 1. Determine pixel type (uint8_t, uint16_t or uint32_t) looking at bpp
	 * 2. get pixel: trfb_color_t pixel = ((type_t*)pixels)[y * width + x];
	 * 3. Get components. For example: r = ((pixel >> rshift) & rmask) << rnorm;
	 * 4. Get color: color = TRFB_RGB(r, g, b);
	 * To set actual pixel you need to:
	 * 1. Determine pixel type (uint8_t, uint16_t or uint32_t) looking at bpp
	 * 2. Get pixel value: trfb_color_t pixel = (((r >> rnorm) & rmask) << rshift) |
	 *                                          (((g >> gnorm) & gmask) << gshift) |
	 *                                          (((b >> bnorm) & bmask) << bshift);
	 * 3. Set pixel value: ((type_t*)pixels)[y * width + x] = pixel;
	 */

	void *pixels;
	/* Free pixels information or not */
	int free_pixels;
} trfb_framebuffer_t;

/* FB8 is BGR233 format: */
#define TRFB_FB8_RMASK  0x07
#define TRFB_FB8_GMASK  0x07
#define TRFB_FB8_BMASK  0x03

#define TRFB_FB8_RSHIFT  0
#define TRFB_FB8_GSHIFT  3
#define TRFB_FB8_BSHIFT  6

#define TRFB_FB16_RMASK  0x1f
#define TRFB_FB16_GMASK  0x3f
#define TRFB_FB16_BMASK  0x1f

#define TRFB_FB16_RSHIFT 11
#define TRFB_FB16_GSHIFT  5
#define TRFB_FB16_BSHIFT  0

#define TRFB_FB32_RMASK  0xff
#define TRFB_FB32_GMASK  0xff
#define TRFB_FB32_BMASK  0xff

#define TRFB_FB32_RSHIFT 16
#define TRFB_FB32_GSHIFT  8
#define TRFB_FB32_BSHIFT  0

typedef uint32_t trfb_color_t;
#define TRFB_RGB(r, g, b) (((r) << 16) | ((g) << 8) | (b))
#define TRFB_COLOR_R(col) (((col) >> TRFB_FB32_RSHIFT) & TRFB_FB32_RMASK)
#define TRFB_COLOR_G(col) (((col) >> TRFB_FB32_GSHIFT) & TRFB_FB32_GMASK)
#define TRFB_COLOR_B(col) (((col) >> TRFB_FB32_BSHIFT) & TRFB_FB32_BMASK)

#define TRFB_PIXEL_FUNCTIONS(bits) \
static inline trfb_color_t trfb_fb##bits##_get_pixel(trfb_framebuffer_t *fb, unsigned x, unsigned y) \
{ \
	register trfb_color_t c = ((uint##bits##_t*)fb->pixels)[y * fb->width + x]; \
 \
	return TRFB_RGB( \
			((c >> fb->rshift) & fb->rmask) << fb->rnorm, \
			((c >> fb->gshift) & fb->gmask) << fb->gnorm, \
			((c >> fb->bshift) & fb->bmask) << fb->bnorm \
		       ); \
} \
 \
static inline void trfb_fb##bits##_set_pixel(trfb_framebuffer_t *fb, unsigned x, unsigned y, trfb_color_t col) \
{ \
	((uint##bits##_t*)fb->pixels)[y * fb->width + x] = \
		(((TRFB_COLOR_R(col) >> fb->rnorm) & fb->rmask) << fb->rshift) | \
		(((TRFB_COLOR_G(col) >> fb->gnorm) & fb->gmask) << fb->gshift) | \
		(((TRFB_COLOR_B(col) >> fb->bnorm) & fb->bmask) << fb->bshift); \
}

TRFB_PIXEL_FUNCTIONS(8)
TRFB_PIXEL_FUNCTIONS(16)
TRFB_PIXEL_FUNCTIONS(32)

static inline trfb_color_t trfb_framebuffer_get_pixel(trfb_framebuffer_t *fb, unsigned x, unsigned y)
{
	if (fb->bpp == 1) {
		return trfb_fb8_get_pixel(fb, x, y);
	} else if (fb->bpp == 2) {
		return trfb_fb16_get_pixel(fb, x, y);
	} else if (fb->bpp == 4) {
		return trfb_fb32_get_pixel(fb, x, y);
	}
	return 0;
}

static inline void trfb_framebuffer_set_pixel(trfb_framebuffer_t *fb, unsigned x, unsigned y, trfb_color_t col)
{
	if (fb->bpp == 1)
		trfb_fb8_set_pixel(fb, x, y, col);
	else if (fb->bpp == 2)
		trfb_fb16_set_pixel(fb, x, y, col);
	else if (fb->bpp == 4)
		trfb_fb32_set_pixel(fb, x, y, col);
	return;
}

typedef struct trfb_client trfb_client_t;
typedef struct trfb_server trfb_server_t;
typedef struct trfb_connection trfb_connection_t;

typedef enum trfb_event_type {
	TRFB_EVENT_NONE = 0,
	TRFB_EVENT_KEY,
	TRFB_EVENT_POINTER,
	TRFB_EVENT_CUT_TEXT
} trfb_event_type_t;

typedef struct trfb_event {
	trfb_event_type_t type;
	union {
		struct trfb_event_key {
			unsigned char down;
			uint32_t code;
		} key;

		struct trfb_event_pointer {
			unsigned char button;
			unsigned x, y;
		} pointer;

		struct trfb_event_cut_text {
			size_t len;
			char *text;
		} cut_text;
	} event;
} trfb_event_t;

struct trfb_server {
	int sock;
	thrd_t thread;

#define TRFB_STATE_STOPPED  0x0000
#define TRFB_STATE_WORKING  0x0001
#define TRFB_STATE_STOP     0x0002
#define TRFB_STATE_ERROR    0x8000
	unsigned state;

	trfb_framebuffer_t *fb;
	unsigned updated;

	mtx_t lock;

	trfb_connection_t *clients;

#define TRFB_EVENTS_QUEUE_LEN 128
	trfb_event_t events[TRFB_EVENTS_QUEUE_LEN];
	unsigned event_cur, event_len;
};

struct trfb_connection {
	trfb_server_t *server;

	trfb_protocol_t version;

	unsigned state;

	/* Client information */
	struct sockaddr addr;
	socklen_t addrlen;
	char name[64];

	thrd_t thread;
	mtx_t lock;

	/*
	 * Array of pixels. Last state for this client.
	 * Width and height are taken from server
	 */
	trfb_framebuffer_t *fb;
	trfb_format_t format;

	trfb_io_t *io;

	trfb_connection_t *next;
};

trfb_server_t *trfb_server_create(size_t width, size_t height, unsigned bpp);
void trfb_server_destroy(trfb_server_t *server);
int trfb_server_start(trfb_server_t *server);
int trfb_server_stop(trfb_server_t *server);
unsigned trfb_server_get_state(trfb_server_t *S);

int trfb_server_lock_fb(trfb_server_t *srv, int w);
int trfb_server_unlock_fb(trfb_server_t *srv);

int trfb_server_add_event(trfb_server_t *srv, trfb_event_t *event);
/* poll_event returns 1 on success and 0 if there is no events or error */
int trfb_server_poll_event(trfb_server_t *srv, trfb_event_t *event);
void trfb_event_clear(trfb_event_t *event);
/* Function copies src to dst and frees src so it is move operation */
int trfb_event_move(trfb_event_t *dst, trfb_event_t *src);

unsigned trfb_server_updated(trfb_server_t *srv);

/* Set socket to listen: */
int trfb_server_set_socket(trfb_server_t *server, int sock);
/* Bind to specified host and address: */
int trfb_server_bind(trfb_server_t *server, const char *host, const char *port);

/* You can set your own error print function. Default is fwrite(message, strlen(message), 1, stderr). */
extern void (*trfb_log_cb)(const char *message);
/* Internal error message writing function:
 * If message startswith "I:" message is information message.
 * If message startswith "E:" message is error message.
 * If message startswith "W:" message is warning message.
 * Default is error message.
 */
void trfb_msg(const char *fmt, ...);

trfb_connection_t* trfb_connection_create(trfb_server_t *srv, int sock, struct sockaddr *addr, socklen_t addrlen);
void trfb_connection_free(trfb_connection_t *con);
/* I/O functions capable to stop thread when you need it */
ssize_t trfb_connection_read(trfb_connection_t *con, void *buf, ssize_t len);
void trfb_connection_read_all(trfb_connection_t *con, void *buf, ssize_t len);
ssize_t trfb_connection_write(trfb_connection_t *con, const void *buf, ssize_t len);
void trfb_connection_write_all(trfb_connection_t *con, const void *buf, ssize_t len);
void trfb_connection_flush(trfb_connection_t *con);

/* Protocol messages: */
ssize_t trfb_send_all(int sock, const void *buf, size_t len);
ssize_t trfb_recv_all(int sock, void *buf, size_t len);

typedef struct trfb_msg_protocol_version {
	trfb_protocol_t proto;
} trfb_msg_protocol_version_t;
int trfb_msg_protocol_version_encode(trfb_msg_protocol_version_t *msg, unsigned char *buf, size_t *len);
int trfb_msg_protocol_version_decode(trfb_msg_protocol_version_t *msg, const unsigned char *buf, size_t len);
int trfb_msg_protocol_version_send(trfb_msg_protocol_version_t *msg, int sock);
int trfb_msg_protocol_version_recv(trfb_msg_protocol_version_t *msg, int sock);

trfb_io_t* trfb_io_socket_wrap(int sock);

/* I/O functions: */
ssize_t trfb_io_read(trfb_io_t *io, void *buf, ssize_t len, unsigned timeout);
ssize_t trfb_io_write(trfb_io_t *io, const void *buf, ssize_t len, unsigned timeout);
void trfb_io_free(trfb_io_t *io);
int trfb_io_flush(trfb_io_t *io, unsigned timeout);
int trfb_io_fgetc(trfb_io_t *io, unsigned timeout);
int trfb_io_fputc(unsigned char c, trfb_io_t *io, unsigned timeout);

#define trfb_io_getc(io, timeout) (((io)->rpos < (io)->rlen)? (io)->rbuf[(io)->rpos++]: trfb_io_fgetc(io, timeout))
#define trfb_io_putc(c, io, timeout) (((io)->wlen < TRFB_BUFSIZ)? ((io)->wbuf[(io)->wlen++] = (c)): trfb_io_fputc(c, io, timeout))

trfb_framebuffer_t* trfb_framebuffer_create(unsigned width, unsigned height, unsigned char bpp);
trfb_framebuffer_t* trfb_framebuffer_create_of_format(unsigned width, unsigned height, trfb_format_t *fmt);
trfb_framebuffer_t* trfb_framebuffer_create_with_data(void *pixels, unsigned width, unsigned height, trfb_format_t *fmt);
void trfb_framebuffer_free(trfb_framebuffer_t *fb);
int trfb_framebuffer_resize(trfb_framebuffer_t *fb, unsigned width, unsigned height);
int trfb_framebuffer_convert(trfb_framebuffer_t *dst, trfb_framebuffer_t *src);
int trfb_framebuffer_format(trfb_framebuffer_t *fb, trfb_format_t *fmt);
void trfb_framebuffer_endian(trfb_framebuffer_t *fb, int is_be);
trfb_framebuffer_t* trfb_framebuffer_copy(trfb_framebuffer_t *fb);

/* extern "C" { */
#ifdef __cplusplus
}
#endif

#endif

