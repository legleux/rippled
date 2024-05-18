FROM debian:12

# CMD ["echo", "Am container running on"]
COPY . /the_sauce

CMD ["uname", "-a"]
