/* SPDX-License-Identifier: GPL-2.0 */

#undef TRACE_SYSTEM
#define TRACE_SYSTEM cros_net

#if !defined(_TRACE_CROS_NET_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_CROS_NET_H

#include <linux/net.h>
#include <linux/if.h>
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
	//protocol is also avable in sk_buff
	//skb_network_header from skba
	// struct iphdr *ip_header = (struct iphdr *) (skb_network_header(skb))
#define CROS_IP_PROTOCOL_NAME(val) { IPPROTO_#val, #val }
#define CROS_SHOW_IP_PROTOCOL(val)				\
	__print_symbolic(val,					\
		{IPPROTO_UDP, "UDP"},				\
		{IPPROTO_TCP, "TCP"},				\
		{IPPROTO_ICMP, "ICMP"},				\
		{IPPROTO_RAW, "RAW"})

TRACE_EVENT(cros_ip6_finish_output2_enter,
	TP_PROTO(struct net *net, struct sock *sk, struct sk_buff *skb),
	TP_ARGS(net, sk, skb),
	TP_STRUCT__entry(
		__array(__u8, saddr, 16)
		__array(__u8, daddr, 16)
		__array(char, dev_name, IFNAMSIZ)
	),
	TP_fast_assign(
		struct ipv6hdr *ip_header = (struct ipv6hdr *)(skb_network_header(skb));
		struct net_device *dev;

		memmove(__entry->saddr, ip_header->saddr.in6_u.u6_addr8, 16);
		memmove(__entry->daddr, ip_header->daddr.in6_u.u6_addr8, 16);
		dev = ((struct dst_entry *)(skb->_skb_refdst & SKB_DST_PTRMASK))->dev;
		strscpy(__entry->dev_name, dev->name, IFNAMSIZ);
	),
	TP_printk("do_not_depend:%pI6c %pI6c %s", __entry->saddr, __entry->daddr,
		  __entry->dev_name))

TRACE_EVENT(cros_ip6_input_finish_enter,
	TP_PROTO(struct net *net, struct sock *sk, struct sk_buff *skb),
	TP_ARGS(net, sk, skb),
	TP_STRUCT__entry(
		__array(__u8, saddr, 16)
		__array(__u8, daddr, 16)
		__array(char, dev_name, IFNAMSIZ)
	),
	TP_fast_assign(
		struct ipv6hdr *ip_header = (struct ipv6hdr *)(skb_network_header(skb));
		struct net_device *dev;

		memmove(__entry->saddr, ip_header->saddr.in6_u.u6_addr8, 16);
		memmove(__entry->daddr, ip_header->daddr.in6_u.u6_addr8, 16);
		dev = skb->dev;
		strscpy(__entry->dev_name, dev->name, IFNAMSIZ);
	),
	TP_printk("do_not_depend:%pI6c %pI6c %s", __entry->saddr, __entry->daddr,
		  __entry->dev_name))

TRACE_EVENT(cros_ip_protocol_deliver_rcu_enter,
	TP_PROTO(struct net *net, struct sk_buff *skb, int protocol),
	TP_ARGS(net, skb, protocol),
	TP_STRUCT__entry(
		__field(__be32, saddr)
		__field(__be32, daddr)
		__array(char, dev_name, IFNAMSIZ)
		__field(int, dev_if)
		__field(short, source_port)
		__field(short, dest_port)
		__field(int, proto)
	),
	TP_fast_assign(
		struct iphdr *ip_header = (struct iphdr *)(skb->head + skb->network_header);
		char *transport = (char *)(skb->head + skb->transport_header);
		struct tcphdr *tcp = (struct tcphdr *)transport;
		struct udphdr *udp = (struct udphdr *)transport;
		struct net_device *dev;
		bool has_transport = skb->transport_header != (typeof(skb->transport_header))~0U;

		__entry->saddr = ip_header->saddr;
		__entry->daddr = ip_header->daddr;
		__entry->proto = ip_header->protocol;
		if (has_transport) {
			switch (ip_header->protocol) {
			case IPPROTO_TCP:
				__entry->source_port = ntohs(tcp->source);
				__entry->dest_port = ntohs(tcp->dest);
				break;
			case IPPROTO_UDP:
				__entry->source_port = ntohs(udp->source);
				__entry->dest_port = ntohs(udp->dest);
				break;
			default:
				break;
			};
		}
		dev = ((struct dst_entry *)(skb->_skb_refdst & SKB_DST_PTRMASK))->dev;
		strscpy(__entry->dev_name, dev->name, IFNAMSIZ);
		__entry->dev_if = dev->ifindex;
	),
	TP_printk("do_not_depend:%pI4 %u %pI4 %u %s %s", &__entry->saddr, __entry->source_port,
		  &__entry->daddr, __entry->dest_port, CROS_SHOW_IP_PROTOCOL(__entry->proto),
		  __entry->dev_name))

TRACE_EVENT(cros__ip_local_out_exit,
	TP_PROTO(struct net *net, struct sock *sk, struct sk_buff *skb, int rv),
	TP_ARGS(net, sk, skb, rv),
	TP_STRUCT__entry(
		__field(__be32, saddr)
		__field(__be32, daddr)
		__field(int, rv)
		__array(char, dev_name, IFNAMSIZ)
		__field(short, source_port)
		__field(short, dest_port)
		__field(int, proto)
	),
	TP_fast_assign(
		struct iphdr *ip_header = (struct iphdr *)(skb->head + skb->network_header);
		char *transport = (char *)(skb->head + skb->transport_header);
		struct tcphdr *tcp = (struct tcphdr *)transport;
		struct udphdr *udp = (struct udphdr *)transport;
		struct net_device *dev;
		bool has_transport = skb->transport_header != (typeof(skb->transport_header))~0U;

		__entry->saddr = ip_header->saddr;
		__entry->daddr = ip_header->daddr;
		__entry->rv = rv;
		__entry->proto = ip_header->protocol;
		if (has_transport) {
			switch (ip_header->protocol) {
			case IPPROTO_TCP:
				__entry->source_port = ntohs(tcp->source);
				__entry->dest_port = ntohs(tcp->dest);
				break;
			case IPPROTO_UDP:
				__entry->source_port = ntohs(udp->source);
				__entry->dest_port = ntohs(udp->dest);
				break;
			default:
				break;
			};
		}
		dev = ((struct dst_entry *)(skb->_skb_refdst & SKB_DST_PTRMASK))->dev;
		strscpy(__entry->dev_name, dev->name, IFNAMSIZ);
	),
	TP_printk("do_not_depend:%pI4 %u %pI4 %u %s %s", &__entry->saddr, __entry->source_port,
		  &__entry->daddr, __entry->dest_port, CROS_SHOW_IP_PROTOCOL(__entry->proto),
		  __entry->dev_name))

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
		"do_not_depend:%d %d %04d",
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
		":%pI4 %d %pI4 %d",
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
		"do_not_depend:%pI4:%d-%pI4:%d-%zu-prot:%d",
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
		"do_not_depend: %pI4 %d %pI4 %d prot:%d issendmsg:%d",
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
		"do_not_depend:%pI4 %d %pI4 %d %zu",
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
		"do_not_depend:%pI4 %d %pI4 %d %zu prot:%d",
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
	TP_printk("do_not_depend:%pI4 %d %pI4 %d",
	&__entry->saddr4, __entry->sport, &__entry->daddr4, __entry->dport));
#endif // _TRACE_CROS_NET_H
/* This part must be outside protection */
#include <trace/define_trace.h>
