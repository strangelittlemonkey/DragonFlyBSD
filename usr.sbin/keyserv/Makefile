# $FreeBSD: src/usr.sbin/keyserv/Makefile,v 1.5.2.1 2001/04/25 12:09:50 ru Exp $

PROG=	keyserv
SRCS=	keyserv.c setkey.c crypt_svc.c crypt_server.c crypt.h
WARNS?=	2

MAN=	keyserv.8

# DO NOT USE -static, dlopen() for _des_crypt symbol
# XXX .undef NOSHARED

CFLAGS+= -DKEYSERV_RANDOM -DBROKEN_DES -I.

DPADD=	${LIBRPCSVC}
LDADD=	-lrpcsvc

DPADD+=		${LIBRECRYPTO}
LDADD+=		-lprivate_crypto
LDFLAGS+=	${PRIVATELIB_LDFLAGS}
CFLAGS+=	-I${.CURDIR}/../../crypto/libressl/include

RPCDIR=	${DESTDIR}/usr/include/rpcsvc

CLEANFILES= crypt_svc.c crypt.h

RPCGEN= rpcgen -C

crypt_svc.c: ${RPCDIR}/crypt.x
	${RPCGEN} -m -o ${.TARGET} ${RPCDIR}/crypt.x

crypt.h: ${RPCDIR}/crypt.x
	${RPCGEN} -h -o ${.TARGET} ${RPCDIR}/crypt.x

.include <bsd.prog.mk>
