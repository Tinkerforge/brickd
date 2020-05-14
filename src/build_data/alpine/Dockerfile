FROM alpine

LABEL version="1.0.0"

RUN printf "foobar1234\nfoobar1234\n" | adduser user
RUN addgroup user abuild
RUN apk add build-base git abuild
RUN mkdir -p /var/cache/distfiles
RUN chmod a+w /var/cache/distfiles
RUN su user -c "abuild-keygen -a -i"
