# $FreeBSD: src/gnu/usr.bin/groff/src/utils/indxbib/Makefile,v 1.7 2003/05/01 13:22:21 ru Exp $

PROG_CXX=	indxbib
SRCS=		indxbib.cpp signal.c
DPADD=		${LIBBIB} ${LIBGROFF} ${LIBM}
LDADD=		${LIBBIB} ${LIBGROFF} -lm
CLEANFILES=	${MAN}
.if !defined(NO_SHARE)
FILES=		${DIST_DIR}/eign
FILESDIR=	${SHAREDIR}/dict
.endif

.include <bsd.prog.mk>
