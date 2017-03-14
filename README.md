This is a test to validate a proxy using splice for copying between client<->proxy<->remote sockets as opposed to
transferring data without splice using read and write syscalls.
The read and write socket approach would incur an additional 2 copies in the kernel to transfer data to and from the user space.

The splice syscall avoids the double copy by using an intermediate pipe buffer to transfer data across 2 socket buffers in kernel.
This approach in general should result in faster throughput but the variance in performance should be dependent on NIC cards.
If the NIC cards support zero-copy or DMA in/out from the pipe buffer to the socket buffer, then the transfer should be fast.

Testing the approach my tcp ping server, tcp ping client through a proxy doesn't seem to see any/big gains.

The CPU performance difference between 2 approaches is minimal.

In fact, I expect the CPU usage to be higher using the splice approach considering the copy_to_user/copy_from_user in the read/write approach is a preemption point and could result in context switches resulting in proxy's bandwidth being lower when running without the splice or the traditional read/write socket approach.

In order to validate it, you can run it on a real hardware since I had run them on a VBOX VM on the same node without incurring the NIC transfers.

To run:
tar zxpvf proxyhello.tar.gz
cd proxyhello
make

Start tcp_ping_server,
./tcp_ping_server -s <server ip>

On another shell, start the proxy:
./proxy -s <ip_address> -z  -r <remote server>
(type -h for help options)

The "-z" option enables splice approach for the proxy to transfer data between client and remote server.
Without -z, it would fall back to traditional approach.

Now on another shell, start the tcp client through the proxy,
./tcp_ping_client -p 9998 -s <ip of proxy> -n 10000

The above would send/recv 10000, 1024 byte packets between client and server through the proxy and would display the bandwidth after the test.
Just use -n 0 to run forever and check cpu usage.

In my testing, I didn't see any perceivable difference though I am sure with real NIC, splice approach would be favorable and would only result in better throughput overall for the proxy.

Nice to have ...

Regards,
-Karthick
