docker build --no-cache -t brickd-static .
docker run --rm --privileged -p 4223:4223 brickd-static:latest
