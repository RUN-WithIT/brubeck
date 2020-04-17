#include <stddef.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/tcp.h>
#include "brubeck.h"

static bool carbon_is_connected(void *backend)
{
	struct brubeck_carbon *self = (struct brubeck_carbon *)backend;
	return (self->out_sock >= 0);
}

static int carbon_connect(void *backend)
{
	struct brubeck_carbon *self = (struct brubeck_carbon *)backend;

	if (carbon_is_connected(self))
		return 0;

	self->out_sock = socket(PF_INET, SOCK_STREAM, IPPROTO_TCP);

	if (self->out_sock >= 0) {
		int rc = connect(self->out_sock,
				(struct sockaddr *)&self->out_sockaddr,
				sizeof(self->out_sockaddr));
		
		if (rc == 0) {
			log_splunk("backend=carbon event=connected");
			sock_enlarge_out(self->out_sock);
			return 0;
		}

		close(self->out_sock);
		self->out_sock = -1;
	}

	log_splunk_errno("backend=carbon event=failed_to_connect");
	return -1;
}

static void carbon_disconnect(struct brubeck_carbon *self)
{
	log_splunk_errno("backend=carbon event=disconnected");

	close(self->out_sock);
	self->out_sock = -1;
}

static void plaintext_each(
	const char *key,
	value_t value,
	void *backend,
        int timestamp)
{
	struct brubeck_carbon *carbon = (struct brubeck_carbon *)backend;
	char buffer[1024];
	char *ptr = buffer;
	size_t key_len = strlen(key);
	ssize_t wr;

	if (!carbon_is_connected(carbon))
		return;

	memcpy(ptr, key, key_len);
	ptr += key_len;
	*ptr++ = ' ';

	ptr += brubeck_ftoa(ptr, value);
	*ptr++ = ' ';

/* this is set to zero.  carbon doc says -1 is time now at server */
	ptr += brubeck_itoa(ptr, carbon->backend.tick_time);
//	ptr += brubeck_itoa(ptr, -1);
	*ptr++ = '\n';

	wr = write_in_full(carbon->out_sock, buffer, ptr - buffer);
	if (wr < 0) {
		carbon_disconnect(carbon);
		return;
	}

	carbon->sent += wr;
}

static inline size_t pickle1_int32(char *ptr, void *_src)
{
	*ptr = 'J';
	memcpy(ptr + 1, _src, 4);
	return 5;
}

static inline size_t pickle1_double(char *ptr, void *_src)
{
	uint8_t *source = _src;

	*ptr++ = 'G';

	ptr[0] = source[7];
	ptr[1] = source[6];
	ptr[2] = source[5];
	ptr[3] = source[4];
	ptr[4] = source[3];
	ptr[5] = source[2];
	ptr[6] = source[1];
	ptr[7] = source[0];

	return 9;
}

static void pickle1_push(
		struct pickler *buf,
		const char *key,
		uint8_t key_len,
		uint32_t timestamp,
		value_t value)
{
	char *ptr = buf->ptr + buf->pos;

	*ptr++ = '(';

	*ptr++ = 'U';
	*ptr++ = key_len;
	memcpy(ptr, key, key_len);
	ptr += key_len;

	*ptr++ = 'q';
	*ptr++ = buf->pt++;

	*ptr++ = '(';

	ptr += pickle1_int32(ptr, &timestamp);
	ptr += pickle1_double(ptr, &value);

	*ptr++ = 't';
	*ptr++ = 'q';
	*ptr++ = buf->pt++;

	*ptr++ = 't';
	*ptr++ = 'q';
	*ptr++ = buf->pt++;

	buf->pos = (ptr - buf->ptr);
}

static inline void pickle1_init(struct pickler *buf)
{
	static const uint8_t lead[] = { ']', 'q', 0, '(' };

	memcpy(buf->ptr + 4, lead, sizeof(lead));
	buf->pos = 4 + sizeof(lead);
	buf->pt = 1;
}

static void pickle1_flush(void *backend)
{
	static const uint8_t trail[] = {'e', '.'};
	struct brubeck_carbon *carbon = (struct brubeck_carbon *)backend;
	struct pickler *buf = &carbon->pickler;

	uint32_t *buf_lead;
	ssize_t wr;
  int onoff = 1;
	
	if (buf->pt == 1 || !carbon_is_connected(carbon))
		return;

	memcpy(buf->ptr + buf->pos, trail, sizeof(trail));
	buf->pos += sizeof(trail);

	buf_lead = (uint32_t *)buf->ptr;
	*buf_lead = htonl((uint32_t)buf->pos - 4);

  /* DAB ... set TCP_NODELAY, but just for flush */
  onoff = 1;
  setsockopt(carbon->out_sock, IPPROTO_TCP, TCP_NODELAY, &onoff, sizeof(onoff));

	wr = write_in_full(carbon->out_sock, buf->ptr, buf->pos);

  /* DAB ... unset */
  onoff = 0;
  setsockopt(carbon->out_sock, IPPROTO_TCP, TCP_NODELAY, &onoff, sizeof(onoff));

	pickle1_init(&carbon->pickler);
	if (wr < 0) {
		carbon_disconnect(carbon);
		return;
	}

	carbon->sent += wr;
}

static void pickle1_each(
	const char *key,
	value_t value,
	void *backend,
        int timestamp)
{
	struct brubeck_carbon *carbon = (struct brubeck_carbon *)backend;
	uint8_t key_len = (uint8_t)strlen(key);

	if (carbon->pickler.pos + PICKLE1_SIZE(key_len)
		>= PICKLE_BUFFER_SIZE) {
		pickle1_flush(carbon);
	}

	if (!carbon_is_connected(carbon))
		return;

	pickle1_push(&carbon->pickler, key, key_len,
		carbon->backend.tick_time, value);
}

struct brubeck_backend *
brubeck_carbon_new(struct brubeck_server *server, json_t *settings)
{
	struct brubeck_carbon *carbon = xcalloc(1, sizeof(struct brubeck_carbon));
	char *address;
	int port, frequency, pickle = 0;

	json_unpack_or_die(settings,
		"{s:s, s:i, s?:b, s:i, s?:i}",
		"address", &address,
		"port", &port,
		"pickle", &pickle,
	        "frequency", &frequency,
	        "expire", &(carbon->backend.expire));

	carbon->backend.type = BRUBECK_BACKEND_CARBON;
	carbon->backend.connect = &carbon_connect;
	carbon->backend.is_connected = &carbon_is_connected;

	if (pickle) {
		carbon->backend.sample = &pickle1_each;
		carbon->backend.flush = &pickle1_flush;
		carbon->pickler.ptr = malloc(PICKLE_BUFFER_SIZE);
		pickle1_init(&carbon->pickler);
	} else {
		carbon->backend.sample = &plaintext_each;
		carbon->backend.flush = NULL;
	}

	carbon->backend.sample_freq = frequency;
	carbon->backend.server = server;
	carbon->out_sock = -1;
	url_to_inaddr2(&carbon->out_sockaddr, address, port);

	brubeck_backend_run_threaded((struct brubeck_backend *)carbon);
	log_splunk("backend=carbon event=started");

	return (struct brubeck_backend *)carbon;
}
