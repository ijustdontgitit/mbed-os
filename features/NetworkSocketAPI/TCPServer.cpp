/* Socket
 * Copyright (c) 2015 ARM Limited
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "TCPServer.h"
#include "Timer.h"

TCPServer::TCPServer()
    : _pending(0), _accept_sem(0)
{
}

TCPServer::TCPServer(NetworkStack *iface)
    : _pending(0), _accept_sem(0)
{
    open(iface);
}

TCPServer::TCPServer(NetworkInterface *iface)
    : _pending(0), _accept_sem(0)
{
    open(iface->get_stack());
}

TCPServer::~TCPServer()
{
    close();
}

int TCPServer::open(NetworkStack *iface)
{
    return Socket::open(iface, NSAPI_TCP);
}

int TCPServer::open(NetworkInterface *iface)
{
    return TCPServer::open(iface->get_stack());
}

int TCPServer::listen(int backlog)
{
    _lock.lock();

    int ret = NSAPI_ERROR_NO_SOCKET;
    if (_socket) {
        ret = _iface->socket_listen(_socket, backlog);
    }

    _lock.unlock();
    return ret;
}

int TCPServer::accept(TCPSocket *connection)
{
    _lock.lock();

    int ret = NSAPI_ERROR_NO_SOCKET;
    while (true) {
        if (!_socket) {
            ret = NSAPI_ERROR_NO_SOCKET;
            break;
        }

        _pending = 0;
        void *socket;
        ret = _iface->socket_accept(&socket, _socket);
        if (0 == ret) {
            connection->_lock.lock();

            if (connection->_socket) {
                connection->close();
            }

            connection->_iface = _iface;
            connection->_socket = socket;
            connection->_event = Callback<void()>(connection, &TCPSocket::event);
            _iface->socket_attach(socket, &Callback<void()>::thunk, &connection->_event);

            connection->_lock.unlock();
            break;
        }

        if (NSAPI_ERROR_WOULD_BLOCK == ret) {
            int32_t count;

            _lock.unlock();
            count = _accept_sem.wait(_timeout);
            _lock.lock();

            if (count < 1) {
                ret = NSAPI_ERROR_WOULD_BLOCK;
                break;
            }
        }
    }

    _lock.unlock();
    return ret;
}

void TCPServer::event()
{
    int32_t acount = _accept_sem.wait(0);
    if (acount <= 1) {
        _accept_sem.release();
    }

    _pending += 1;
    if (_callback && _pending == 1) {
        _callback();
    }
}