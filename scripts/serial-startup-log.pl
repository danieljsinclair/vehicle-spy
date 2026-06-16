#!/usr/bin/env perl
use strict;
use warnings;
use Getopt::Long qw(GetOptions);
use IO::Select;

my ($port, $baud, $max_wait, $post_byte, $reset_esptool, $esptool, $help);
$baud      = 115200;
$max_wait  = 30;
$post_byte = 30;
$esptool   = ($ENV{HOME} // '/Users/danielsinclair') . '/Library/Arduino15/packages/esp32/tools/esptool_py/5.2.0/esptool';

sub usage {
    print <<"USAGE";
Usage: serial-startup-log.pl --port /dev/cu.usbserial-10 [options]

Options:
  --port PORT          Serial port to read. Required.
  --baud BAUD          Baud rate. Default: 115200.
  --max-wait SECONDS   Wait this long for the first byte. Default: 30.
  --post-byte SECONDS  Read this long after the first byte. Default: 30.
  --reset-esptool      Start esptool run while the serial logger is attached.
  --esptool PATH       esptool binary path. Default: Arduino ESP32 esptool.
  --help               Show this help.
USAGE
}

GetOptions(
    'port=s'        => \$port,
    'baud=i'        => \$baud,
    'max-wait=i'    => \$max_wait,
    'post-byte=i'   => \$post_byte,
    'reset-esptool' => \$reset_esptool,
    'esptool=s'     => \$esptool,
    'help'          => \$help,
) or do { usage(); exit 2; };

if ($help || !$port) {
    usage();
    exit($help ? 0 : 2);
}

if ($reset_esptool) {
    die "esptool not found: $esptool\n" unless -x $esptool;
    print "Running: $esptool --port $port --baud $baud run\n";
    system($esptool, '--port', $port, '--baud', $baud, 'run');
}

open(my $fh, '+<', $port) or die "failed to open $port: $!\n";
system('stty', '-f', $port, $baud, 'raw', '-echo', '-icanon', '-opost', '-ixon', '-ixoff', '-crtscts') == 0
    or die "failed to configure $port: $!\n";
$| = 1;

my $sel = IO::Select->new($fh);
my $deadline = time() + $max_wait;
my $stop_at;

while (time() < $deadline && !defined $stop_at) {
    if ($sel->can_read(0.1)) {
        my $buf = '';
        my $n = sysread($fh, $buf, 4096);
        if (defined $n && $n > 0) {
            print $buf;
            $stop_at = time() + $post_byte;
        }
    }
}

if (!defined $stop_at) {
    while (time() < $deadline) {
        if ($sel->can_read(0.1)) {
            my $buf = '';
            my $n = sysread($fh, $buf, 4096);
            if (defined $n && $n > 0) {
                print $buf;
                $stop_at = time() + $post_byte;
                last;
            }
        }
    }
}

if (defined $stop_at) {
    while (time() < $stop_at) {
        if ($sel->can_read(0.1)) {
            my $buf = '';
            my $n = sysread($fh, $buf, 4096);
            print $buf if defined $n && $n > 0;
        }
    }
} else {
    warn "warning: no startup bytes received from $port within $max_wait seconds\n";
}

exit 0;
