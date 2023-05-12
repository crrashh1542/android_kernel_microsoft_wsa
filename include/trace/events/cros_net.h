/* SPDX-License-Identifier: GPL-2.0 */

#undef TRACE_SYSTEM
#define TRACE_SYSTEM cros_net

#if !defined(_TRACE_CROS_NET_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_CROS_NET_H

#include <linux/net.h>
#include <linux/tracepoint.h>

// @rv: return value of the socket listen.
// @dev_if: bound device index.
// @type: socket type (%SOCK_STREAM, etc.).
// @port: the bind port.


#define CROS_NET_FILL_ADDR_PORT(sk, entry)\
	do {							\
		entry->sport = sk->__sk_common.skc_num;		\
		entry->saddr4 = sk->__sk_common.skc_rcv_saddr;	\
		entry->dport = sk->__sk_common.skc_dport;	\
		entry->daddr4 = sk->__sk_common.skc_daddr;	\
		entry->protocol = sk->sk_protocol;		\
	} while (0)


TRACE_EVENT(cros_inet_listen_exit,
	/* The tracepoint signature matches the signature of inet_listen with
	 * addition of the return value. This is done to match the expected
	 * signature of an fexit bpf program.
	 * This is done so that a BPF application can use the same
	 * handler for kernels that don't support fexit for ARM64 and those
	 * that do.
	 */
	TP_PROTO(struct socket *socket, int backlog, int rv),
	TP_ARGS(socket, backlog, rv),
	TP_STRUCT__entry(
		__field(int,	dev_if)
		__field(short, type)
		__field(__u16,	port)
	),
	TP_fast_assign(
		__entry->dev_if = socket->sk->__sk_common.skc_bound_dev_if;
		__entry->type = socket->type;
		__entry->port = socket->sk->__sk_common.skc_num;
	),
	TP_printk(
		"cros_inet_listen:informational_only_will_be_removed:%d:%d:%04d",
		__entry->dev_if,
		__entry->type,
		__entry->port)
);

TRACE_EVENT_CONDITION(cros_inet_accept_exit,
	/* The tracepoint signature matches the signature of inet_accept with
	 * addition of the return value. This is done to match the expected
	 * signature of an fexit bpf program.
	 * This is done so that a BPF application can use the same
	 * handler for kernels that don't support fexit for ARM64 and those
	 * that do.
	 */
	TP_PROTO(struct socket *sock, struct socket *newsock, int flags, bool kern, int rv),
	TP_ARGS(sock, newsock, flags, kern, rv),
	TP_CONDITION(newsock && newsock->sk),
	TP_STRUCT__entry(
	__field(__u32, saddr4)
	__field(__u32, daddr4)
	__field(__u16, sport)
	__field(__u16, dport)
	__field(__u8, protocol)
	),
	TP_fast_assign(
		struct sock *sk = newsock->sk;

		CROS_NET_FILL_ADDR_PORT(sk, __entry);
	),
	TP_printk(
		"informational_only_will_be_removed:%pi4:%d-%pi4:%d",
		&__entry->saddr4,
		__entry->sport,
		&__entry->daddr4,
		__entry->dport)
);


TRACE_EVENT_CONDITION(cros_inet_sendmsg_exit,
	/* The tracepoint signature matches the signature of inet_sendmsg with
	 * addition of the return value. This is done to match the expected
	 * signature of an fexit bpf program.
	 * This is done so that a BPF application can use the same
	 * handler for kernels that don't support fexit for ARM64 and those
	 * that do.
	 */
	TP_PROTO(struct socket *sock, struct msghdr *msg, size_t size, int rv),
	TP_ARGS(sock, msg, size, rv),
	TP_CONDITION(sock && sock->sk),
	TP_STRUCT__entry(
		__field(size_t, bytes_sent)
		__field(__u32, saddr4)
		__field(__u32, daddr4)
		__field(__u16, sport)
		__field(__u16, dport)
		__field(__u8, protocol)
	),
	TP_fast_assign(
		struct sock *sk = sock->sk;

		CROS_NET_FILL_ADDR_PORT(sk, __entry);
		__entry->bytes_sent = rv;
	),
	TP_printk(
		"informational_only_will_be_removed:%pi4:%d-%pi4:%d-%z-prot:%d",
		&__entry->saddr4,
		__entry->sport,
		&__entry->daddr4,
		__entry->dport,
		__entry->bytes_sent,
		__entry->protocol
		)
);

TRACE_EVENT_CONDITION(cros_inet_stream_connect_exit,
	/* The tracepoint signature matches the signature of inet_stream_connect
	 * with the addition of the return value. This is done to match the
	 * expected signature of an fexit bpf program.
	 * This is done so that a BPF application can use the same
	 * handler for kernels that don't support fexit for ARM64 and those
	 * that do.
	 */
	TP_PROTO(struct socket *sock, struct sockaddr *uaddr, int addr_len, int flags,
		int is_sendmsg,  int rv),
	TP_ARGS(sock, uaddr, addr_len, flags, is_sendmsg, rv),
	TP_CONDITION(sock && sock->sk),
	TP_STRUCT__entry(
		__field(int, is_sendmsg)
		__field(__u32, saddr4)
		__field(__u32, daddr4)
		__field(__u16, sport)
		__field(__u16, dport)
		__field(__u8, protocol)
	),
	TP_fast_assign(
		struct sock *sk = sock->sk;

		CROS_NET_FILL_ADDR_PORT(sk, __entry);
		__entry->is_sendmsg = is_sendmsg;
	),
	TP_printk(
		"informational_only_will_be_removed:%pi4:%d-%pi4:%d-prot:%d-issendmsg:%d",
		&__entry->saddr4,
		__entry->sport,
		&__entry->daddr4,
		__entry->dport,
		__entry->protocol,
		__entry->is_sendmsg
		)
);

TRACE_EVENT_CONDITION(cros_inet_sendpage_exit,
	/* The tracepoint signature matches the signature of inet_sendpage with
	 * addition of the return value. This is done to match the expected
	 * signature of an fexit bpf program.
	 * This is done so that a BPF application can use the same
	 * handler for kernels that don't support fexit for ARM64 and those
	 * that do.
	 */
	TP_PROTO(struct socket *sock, struct page *page, int offset, size_t size,
		int flags, int rv),
	TP_ARGS(sock, page, offset, size, flags, rv),
	TP_CONDITION(sock && sock->sk),
	TP_STRUCT__entry(
		__field(size_t, bytes_sent)
		__field(__u32, saddr4)
		__field(__u32, daddr4)
		__field(__u16, sport)
		__field(__u16, dport)
		__field(__u8, protocol)
	),
	TP_fast_assign(
		struct sock *sk = sock->sk;

		CROS_NET_FILL_ADDR_PORT(sk, __entry);
		__entry->bytes_sent = rv;
	),
	TP_printk(
		"informational_only_will_be_removed:%pi4:%d-%pi4:%d-%z",
		&__entry->saddr4,
		__entry->sport,
		&__entry->daddr4,
		__entry->dport,
		__entry->bytes_sent
		)
);

TRACE_EVENT_CONDITION(cros_inet_recvmsg_exit,
	/* The tracepoint signature matches the signature of inet_recvmsg with
	 * addition of the return value. This is done to match the expected
	 * signature of an fexit bpf program.
	 * This is done so that a BPF application can use the same
	 * handler for kernels that don't support fexit for ARM64 and those
	 * that do.
	 */
	TP_PROTO(struct socket *sock, struct msghdr *msg, size_t size, int flags, int rv),
	TP_ARGS(sock, msg, size, flags, rv),
	TP_CONDITION(sock && sock->sk),
	TP_STRUCT__entry(
		__field(size_t, bytes_sent)
		__field(__u32, saddr4)
		__field(__u32, daddr4)
		__field(__u16, sport)
		__field(__u16, dport)
		__field(__u8, protocol)
	),
	TP_fast_assign(
		struct sock *sk = sock->sk;

		CROS_NET_FILL_ADDR_PORT(sk, __entry);
		__entry->bytes_sent = rv;
	),
	TP_printk(
		"informational_only_will_be_removed:%pi4:%d-%pi4:%d-%z-prot:%d",
		&__entry->saddr4,
		__entry->sport,
		&__entry->daddr4,
		__entry->dport,
		__entry->bytes_sent,
		__entry->protocol
		)
);

TRACE_EVENT_CONDITION(cros_inet_release_enter,
	TP_PROTO(struct socket *sock),
	TP_ARGS(sock),
	TP_CONDITION(sock && sock->sk),
	TP_STRUCT__entry(
		__field(__u32, saddr4)
		__field(__u32, daddr4)
		__field(__u16, sport)
		__field(__u16, dport)
		__field(__u8, protocol)
	),
	TP_fast_assign(
		struct sock *sk = sock->sk;

		CROS_NET_FILL_ADDR_PORT(sk, __entry);
	),
	TP_printk("informational_only_will_be_removed:%pI4:%d-%pI4:%d",
	&__entry->saddr4, __entry->sport, &__entry->daddr4, __entry->dport));
#endif // _TRACE_CROS_NET_H
/* This part must be outside protection */
#include <trace/define_trace.h>
