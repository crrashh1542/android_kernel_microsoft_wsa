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


TRACE_EVENT(cros_inet_listen,

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
#endif // _TRACE_CROS_NET_H

/* This part must be outside protection */
#include <trace/define_trace.h>
