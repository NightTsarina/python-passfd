#!/usr/bin/env python
# vim: set fileencoding=utf-8
# vim: ts=4:sw=4:et:ai:sts=4

# Copyright © 2010 Martín Ferrari <martin.ferrari@gmail.com>
#
# This program is free software; you can redistribute it and/or modify it
# under the terms of the GNU General Public License as published by the Free
# Software Foundation; either version 2 of the License, or (at your option)
# any later version.
#
# This program is distributed in the hope that it will be useful, but WITHOUT
# ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
# FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
# more details.
#
# You should have received a copy of the GNU General Public License along with
# this program; if not, write to the Free Software Foundation, Inc., 51
# Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
#

import os, unittest, socket, sys
from passfd import sendfd, recvfd
import _passfd

class TestPassfd(unittest.TestCase):
    def readfd_test(self, fd):
        s = fd.read(512)
        self.assertEquals(len(s), 512)
        for i in s:
            self.assertEquals(i, "\0")

    def vrfy_recv(self, tuple, msg):
        self.readfd_test(tuple[0])
        self.assertEquals(tuple[1], msg)

    def parent_tests(self, s):
        # First message is not even sent
        s.send("a")
        self.vrfy_recv(recvfd(s), "a")
        s.send("a")
        self.vrfy_recv(recvfd(s), "\0")
        s.send("a")
        self.vrfy_recv(recvfd(s), "foobar")
        s.send("a")
        self.vrfy_recv(recvfd(s, msg_buf = 11), "long string") # is long
        self.assertEquals(s.recv(8), " is long") # re-sync
        s.send("a")
        self.assertEquals(s.recv(100), "foobar")
        s.send("a")
        self.assertRaises(RuntimeError, recvfd, s) # No fd received
        #
        s.send("a")
        self.assertRaises(OSError, recvfd, s, 4096, ['w']) # Trying to write
        s.send("a")
        (f, msg) = recvfd(s, open_args = [ "w" ])
        self.assertEquals(msg, "writing")
        f.write("foo")
        s.send("a")

    def child_tests(self, s):
        f = file("/dev/zero")
        assert sendfd(s, f, "") == 0
        s.recv(1)
        assert sendfd(s, f, "a") == 1
        s.recv(1)
        assert sendfd(s, f, "\0") == 1
        s.recv(1)
        assert sendfd(s, f, "foobar") == 6
        s.recv(1)
        assert sendfd(s, f, "long string is long") == 19
        # The other side will recv() instead of recvmsg(), this fd would be
        # lost. I couldn't find any specification on this semantic
        s.recv(1)
        assert sendfd(s, f, "foobar") == 6
        s.recv(1)
        assert s.send("barbaz") == 6
        # Try to write!
        s.recv(1)
        assert sendfd(s, f, "writing") == 7
        s.recv(1)
        f = file("/dev/null", "w")
        assert sendfd(s, f, "writing") == 7
        s.recv(1)

    def test_sanity_checks(self):
        self.assertRaises(TypeError, recvfd, "foo")
        s = socket.socket(socket.AF_INET)
        self.assertRaises(ValueError, recvfd, s)

        (s0, s1) = socket.socketpair(socket.AF_UNIX, socket.SOCK_STREAM, 0)
        f = file("/dev/zero")
        sendfd(s0, f)
        recvfd(s1)

        # Using integers
        sendfd(s0.fileno(), f.fileno())
        recvfd(s1.fileno())

        self.assertRaises(TypeError, sendfd, s0, "foo")
        # Assuming fd 255 is not valid
        self.assertRaises(OSError, sendfd, s0, 255)

    def test_passfd_stream(self):
        (s0, s1) = socket.socketpair(socket.AF_UNIX, socket.SOCK_STREAM, 0)
        pid = os.fork()
        if pid == 0:
            s0.close()
            self.child_tests(s1)
            s1.close()
            os._exit(0)

        s1.close()
        self.parent_tests(s0)
        s0.close()

        self.assertEquals(os.waitpid(pid, 0)[1], 0)

    def _test_passfd_dgram(self):
        (s0, s1) = socket.socketpair(socket.AF_UNIX, socket.SOCK_DGRAM, 0)
        pid = os.fork()
        if pid == 0:
            s0.close()
            self.child_tests(s1)
            s1.close()
            os._exit(0)

        s1.close()
        self.parent_tests(s0)
        s0.close()

        self.assertEquals(os.waitpid(pid, 0)[1], 0)

if __name__ == '__main__':
    unittest.main()

