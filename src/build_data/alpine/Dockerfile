FROM alpine:3.11.6

RUN printf "foobar1234\nfoobar1234\n" | adduser user
RUN addgroup user abuild
RUN apk add build-base git abuild
RUN mkdir -p /var/cache/distfiles
RUN chmod a+w /var/cache/distfiles
RUN su user -c "abuild-keygen -a -i"
COPY brickd /brickd
RUN chown user:user /brickd
RUN su user -c "cd /brickd && abuild fetch && abuild checksum && abuild -r"
