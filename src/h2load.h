/*
 * nghttp2 - HTTP/2 C Library
 *
 * Copyright (c) 2014 Tatsuhiro Tsujikawa
 *
 * Permission is hereby granted, free of charge, to any person obtaining
 * a copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sublicense, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * The above copyright notice and this permission notice shall be
 * included in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE
 * LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION
 * OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION
 * WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */
#ifndef H2LOAD_H
#define H2LOAD_H

#include "nghttp2_config.h"

#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>

#include <vector>
#include <string>
#include <unordered_map>
#include <memory>

#include <nghttp2/nghttp2.h>

#include <ev.h>

#include <openssl/ssl.h>

#include "http2.h"
#include "ringbuf.h"

using namespace nghttp2;

namespace h2load {

class Session;

struct Config {
  std::vector<std::vector<nghttp2_nv>> nva;
  std::vector<std::vector<const char *>> nv;
  nghttp2::Headers custom_headers;
  std::string scheme;
  std::string host;
  std::string ifile;
  addrinfo *addrs;
  size_t nreqs;
  size_t nclients;
  size_t nthreads;
  // The maximum number of concurrent streams per session.
  ssize_t max_concurrent_streams;
  size_t window_bits;
  size_t connection_window_bits;
  enum { PROTO_HTTP2, PROTO_SPDY2, PROTO_SPDY3, PROTO_SPDY3_1 } no_tls_proto;
  uint16_t port;
  uint16_t default_port;
  bool verbose;

  Config();
  ~Config();
};

struct Stats {
  // The total number of requests
  size_t req_todo;
  // The number of requests issued so far
  size_t req_started;
  // The number of requests finished
  size_t req_done;
  // The number of requests marked as success. This is subset of
  // req_done.
  size_t req_success;
  // The number of requests failed. This is subset of req_done.
  size_t req_failed;
  // The number of requests failed due to network errors. This is
  // subset of req_failed.
  size_t req_error;
  // The number of bytes received on the "wire". If SSL/TLS is used,
  // this is the number of decrypted bytes the application received.
  int64_t bytes_total;
  // The number of bytes received in HEADERS frame payload.
  int64_t bytes_head;
  // The number of bytes received in DATA frame.
  int64_t bytes_body;
  // The number of each HTTP status category, status[i] is status code
  // in the range [i*100, (i+1)*100).
  size_t status[6];
};

enum ClientState { CLIENT_IDLE, CLIENT_CONNECTED };

struct Client;

struct Worker {
  std::vector<std::unique_ptr<Client>> clients;
  Stats stats;
  struct ev_loop *loop;
  SSL_CTX *ssl_ctx;
  Config *config;
  size_t progress_interval;
  uint32_t id;
  bool tls_info_report_done;

  Worker(uint32_t id, SSL_CTX *ssl_ctx, size_t nreq_todo, size_t nclients,
         Config *config);
  ~Worker();
  void run();
};

struct Stream {
  int status_success;
  Stream();
};

struct Client {
  std::unordered_map<int32_t, Stream> streams;
  std::unique_ptr<Session> session;
  ev_io wev;
  ev_io rev;
  std::function<int(Client &)> readfn, writefn;
  std::function<int(Client &, const uint8_t *, size_t)> on_readfn;
  std::function<int(Client &)> on_writefn;
  Worker *worker;
  SSL *ssl;
  addrinfo *next_addr;
  size_t reqidx;
  ClientState state;
  // The number of requests this client has to issue.
  size_t req_todo;
  // The number of requests this client has issued so far.
  size_t req_started;
  // The number of requests this client has done so far.
  size_t req_done;
  int fd;
  RingBuf<65536> wb;

  enum { ERR_CONNECT_FAIL = -100 };

  Client(Worker *worker, size_t req_todo);
  ~Client();
  int connect();
  void disconnect();
  void fail();
  void submit_request();
  void process_abandoned_streams();
  void report_progress();
  void report_tls_info();
  void terminate_session();

  int do_read();
  int do_write();

  int connected();
  int read_clear();
  int write_clear();
  int tls_handshake();
  int read_tls();
  int write_tls();

  int on_read(const uint8_t *data, size_t len);
  int on_write();
  int on_connect();
  int noop();

  void on_request(int32_t stream_id);
  void on_header(int32_t stream_id, const uint8_t *name, size_t namelen,
                 const uint8_t *value, size_t valuelen);
  void on_stream_close(int32_t stream_id, bool success);

  void signal_write();
};

} // namespace h2load

#endif // H2LOAD_H
