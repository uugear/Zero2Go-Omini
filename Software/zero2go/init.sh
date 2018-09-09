#!/bin/bash
# /etc/init.d/zero2go_daemon

### BEGIN INIT INFO
# Provides:          zero2go_daemon
# Required-Start:    $remote_fs $syslog
# Required-Stop:     $remote_fs $syslog
# Default-Start:     2 3 4 5
# Default-Stop:      0 1 6
# Short-Description: Zero2Go Omini Deamon initialize script
# Description:       This service is used to manage Zero2Go Omini Daemon service
### END INIT INFO

case "$1" in
    start)
        echo "Starting Zero2Go Omini Daemon..."
        sudo /home/pi/zero2go/daemon.sh &
	sleep 1
	zero2GoPid=$(ps --ppid $! -o pid=)
	echo $zero2GoPid > /var/run/zero2go_daemon.pid
        ;;
    stop)
        echo "Stopping Zero2Go Omini Daemon..."
	zero2GoPid=$(cat /var/run/zero2go_daemon.pid)
	kill -9 $zero2GoPid
        ;;
    *)
        echo "Usage: /etc/init.d/zero2go_daemon start|stop"
        exit 1
        ;;
esac

exit 0
