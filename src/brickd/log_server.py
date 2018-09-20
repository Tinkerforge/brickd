#!/usr/bin/python3
# -*- coding: utf-8 -*-

import sys
import socket

if __name__ == '__main__':
    server = socket.socket(socket.AF_INET, socket.SOCK_STREAM)

    server.bind(('0.0.0.0', 11111))
    server.listen(10)

    while True:
        print('>>> accepting ...')

        client, address = server.accept()

        print('>>> connected')

        while True:
            try:
                data = client.recv(2048)
            except Exception as e:
                print('>>> receive error: {0}'.format(e))
                break

            if len(data) == 0:
                print('>>> disconnected')
                break

            sys.stdout.buffer.write(data)
            sys.stdout.buffer.flush()
