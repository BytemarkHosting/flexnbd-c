FLEXNBD(1)
==========
:doctype: manpage

NAME
----
flexnbd - A fast NBD server

SYNOPSIS
--------
*flexnbd* 'COMMAND' ['OPTIONS']

DESCRIPTION
-----------
Flexnbd is a fast NBD server which supports live migration. Live
migration is performed by writing the data to a new server. A failed
migration will be invisible to any connected clients.

Flexnbd tries quite hard to preserve sparsity of files it is serving,
even across migrations.

COMMANDS
--------

serve
~~~~~
  $ flexnbd serve --addr <ADDR> --port <PORT> --file <FILE> 
    [--sock <SOCK>] [--default-deny] [global option]* [acl entry]*

Serve a file. If any ACL entries are given (which should be IP
addresses), only those clients listed will be permitted to connect.

flexnbd will continue to serve until a SIGINT, SIGQUIT, or a successful
migration.

Options
^^^^^^^

*--addr, -l ADDR*:
    The address to listen on. Required.

*--port, -p PORT*:
    The port to listen on. Required.

*--file, -f FILE*:
    The file to serve. Must already exist. Required.

*--sock, -s SOCK*:
    Path to a control socket to open.  You will need this if you want to
    migrate, get the current status, or manipulate the access control
    list.

*--default-deny, -d*:
    How to interpret an empty ACL.  If --default-deny is given, an
    empty ACL will let no clients connect.  If it is not given, an
    empty ACL will let any client connect.
    
listen
~~~~~~

  $ flexnbd listen --addr <ADDR> --port <PORT> --file <FILE> 
    [--sock <SOCK>] [--default-deny] [global option]* [acl entry]*

Listen for an inbound migration, and quit with a status of 0 on
completion.

flexnbd will wait for a successful migration, and then quit. The file
to write the inbound migration data to must already exist before you
run 'flexnbd listen'.

Only one sender may connect to send data, and if the sender
disconnects part-way through the migration, the destination will
expect it to reconnect and retry the whole migration.  It isn't safe
to assume that a partial migration can be resumed because the
destination has no knowledge of whether a client has made a write to
the source in the interim.

If the migration fails for a reason which the `flexnbd listen` process
can't fix (say, a failed local write), it will exit with an error
status.  In this case, the sender will continually retry the migration
until it succeeds, and you will need to restart the `flexnbd listen`
process to allow that to happen.

Options
^^^^^^^
As for 'serve'.

mirror
~~~~~~

  $ flexnbd mirror --addr <ADDR> --port <PORT> --sock SOCK 
      [--bind <BIND-ADDR>] [global option]*

Start a migration from the server with control socket SOCK to the server
listening at ADDR:PORT.

Migration can be a slow process. Rather than block the 'flexnbd mirror'
process until it completes, it will exit with a message of "Migration
started" once it has confirmation that the local server was able to
connect to ADDR:PORT and got an NBD header back.  To check on the
progress of a running migration, use 'flexnbd status'.

If the destination unexpectedly disconnects part-way through the
migration, the source will attempt to reconnect and start the migration
again.  It is not safe to resume the migration from where it left off
because the source can't see that the backing store behind the
destination is intact, or even on the same machine.

Note: files smaller than 4096 bytes cannot be migrated.

Options
^^^^^^^

*--addr, -l ADDR*:
  The address of the remote server to migrate to. Required.

*--port, -p PORT*:
  The port of the remote server to migrate to. Required.

*--sock, -s SOCK*:
  The control socket of the local server to migrate from. Required.

*--bind, -b BIND-ADDR*:
  The local address to bind to. You may need this if the remote server
  is using an access control list.

break
~~~~~

  $ flexnbd mirror --sock SOCK [global option]*

Stop a running migration.

Options
^^^^^^^

*--sock, -s SOCK*:
  The control socket of the local server whose emigration to stop.
  Required.


acl
~~~

  $ flexnbd acl --sock <SOCK> [acl entry]+ [global option]*

Set the access control list of the server with the control socket SOCK
to the given access control list entries.

ACL entries are given as IP addresses.

Options
^^^^^^^

*--sock, -s SOCK*:
  The control socket of the server whose ACL to replace.

status
~~~~~~

  $ flexnbd status --sock <SOCK> [global option]*

Get the current status of the server with control socket SOCK.

The status will be printed to STDOUT.  It is a space-separated list of
key=value pairs.  The space character will never appear in a key or
value.  Currently reported values are:

*pid*:
  The process id of the server listening on SOCK.

*is_mirroring*:
  'true' if this server is sending migration data, 'false' otherwise.

*has_control*:
  'false' if this server was started in 'listen' mode. 'true' otherwise.

read
~~~~

  $ flexnbd read --addr <ADDR> --port <PORT> --from <OFFSET> 
    --size <SIZE>  [--bind BIND-ADDR] [global option]*

Connect to the server at ADDR:PORT, and read SIZE bytes starting at
OFFSET in a single NBD query.  The returned data will be echoed to
STDOUT.  In case of a remote ACL, set the local source address to
BIND-ADDR.

Options
^^^^^^^

*--addr, -l ADDR*:
  The address of the remote server.  Required.

*--port, -p PORT*:
  The port of the remote server.  Required.

*--from, -F OFFSET*:
  The byte offset to start reading from. Required. Maximum 2^62.

*--size, -S SIZE*:
  The number of bytes to read. Required.  Maximum 2^30.

*--bind, -b BIND-ADDR*:
  The local address to bind to. You may need this if the remote server
  is using an access control list.

write
~~~~~

  $ cat ... | flexnbd write --addr <ADDR> --port <PORT> --from <OFFSET> 
    --size <SIZE>  [--bind BIND-ADDR] [global option]*

Connect to the server at ADDR:PORT, and write SIZE bytes from STDIN
starting at OFFSET in a single NBD query.  In case of a remote ACL, set
the local source address to BIND-ADDR.

Options
^^^^^^^

*--addr, -l ADDR*:
  The address of the remote server.  Required.

*--port, -p PORT*:
  The port of the remote server.  Required.

*--from, -F OFFSET*:
  The byte offset to start writing from. Required. Maximum 2^62.

*--size, -S SIZE*:
  The number of bytes to write. Required.  Maximum 2^30.

*--bind, -b BIND-ADDR*:
  The local address to bind to. You may need this if the remote server
  is using an access control list.

help
~~~~

  $ flexnbd help [command] [global option]*

Without 'command', show the list of available commands.  With 'command',
show help for that command.

GLOBAL OPTIONS
--------------

*--help, -h* :
    Show command or global help.

*--verbose, -v* :
    Output all available log information to STDERR.

*--quiet, -q* :
    Output as little log information as possible to STDERR.


LOGGING
-------
Log output is sent to STDERR.  If --quiet is set, no output will be seen
unless the program termintes abnormally.  If neither --quiet nor
--verbose are set, no output will be seen unless something goes wrong
with a specific request.  If --verbose is given, every available log
message will be seen (which, for a debug build, is many).  It is not an
error to set both --verbose and --quiet.  The last one wins.

The log line format is:

  <LEVEL>:<PID> <THREAD> <SOURCEFILE>:<SOURCELINE>: <MSG>

*LEVEL*:
  This will be one of 'D', 'I', 'W', 'E', 'F' in increasing order of
severity.  If flexnbd is started with the --quiet flag, only 'F' will be
seen.  If it is started with the --verbose flag, any from 'I' upwards
will be seen.  Only if you have a debug build and start it with
--verbose will you see 'D' entries.

*PID*:
  This is the process ID.

*THREAD*:
  There are several pthreads per flexnbd process: a main thread, a serve
thread, a thread per client, and possibly a pair of mirror threads and a
control thread.  This field identifies which thread was responsible for
the log line.

*SOURCEFILE:SOURCELINE*:
  Identifies where in the source code this log line can be found.

*MSG*: 
  A short message describing what's happening, how it's being done, or
if you're very lucky *why* it's going on.  

EXAMPLES
--------

Serving a file
~~~~~~~~~~~~~~

The simplest case is serving a file on the default nbd port:

  $ cp /etc/passwd /tmp
  $ flexnbd serve --file /tmp/passwd --addr 0.0.0.0 --port 4777 &
  $ flexnbd read --addr 127.0.0.1 --port 4777 --from 0 --size 7
  root:x:
  $

Reading server status
~~~~~~~~~~~~~~~~~~~~~

In order to read a server's status, we need it to open a control socket.

  $ flexnbd serve --file /tmp/passwd --addr 0.0.0.0 --port 4777 \
    --sock /tmp/flexnbd.sock
  $ flexnbd status --sock /tmp/flexnbd.sock
  pid=9635 is_mirroring=false has_control=true

  $

Note that the status output is newline-terminated.

Migrating
~~~~~~~~~

To migrate, we need to provide a destination file of the right size.

  $ dd if=/dev/urandom of=/tmp/data bs=1024 count=1K
  $ truncate -s 1M /tmp/data.copy
  $ flexnbd serve --file /tmp/data --addr 0.0.0.0 --port 4778 \
      --sock /tmp/flex-source.sock &
  $ flexnbd listen --file /tmp/data.copy --addr 0.0.0.0 --port 4779 \
      --sock /tmp/flex-dest.sock &
  $

Now we check the status of each server, to check that they are both in
the right state:
 
  $ flexnbd status --sock /tmp/flex-source.sock
  pid=9648 is_mirroring=false has_control=true
  $ flexnbd status --sock /tmp/flex-dest.sock
  pid=9651 is_mirroring=false has_control=false 
  $

With this knowledge in hand, we can start the migration:

  $ flexnbd mirror --addr 127.0.0.1 --port 4779 \
      --sock /tmp/flex-source.sock
  Migration started
  [1]  + 9648 done       build/flexnbd serve --addr 0.0.0.0 --port 4778
  [2]  + 9651 done       build/flexnbd listen --addr 0.0.0.0 --port 4779
  $

Note that because the file is so small in this case, we see the source
server quit soon after we start the migration, and the destination
exited at roughly the same time.

BUGS
----

Should be reported to alex@bytemark.co.uk.

AUTHOR
------

Written by Alex Young <alex@bytemark.co.uk>.  
Original concept and core code by Matthew Bloch
<matthew@bytemark.co.uk>.

COPYING
-------

Copyright (c) 2012 Bytemark Hosting Ltd. Free use of this software is
granted under the terms of the GNU General Public License version 3 or
later.
