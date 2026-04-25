#!/usr/bin/env perl
use strict;
use warnings;
use File::Path qw(make_path);
use File::Spec;

sub usage {
    my ($argv0) = @_;
    print "Usage:\n";
    print "  $argv0 <input.txl> <output_dir>\n";
    exit 1;
}

my $input = shift @ARGV // usage($0);
my $out_dir = shift @ARGV // usage($0);

open(my $fh, '<:raw', $input) or die "Failed to open '$input': $!\n";
local $/;
my $data = <$fh>;
close($fh);

my $size = length($data);
die "Input '$input' is too small.\n" if $size < 64;

make_path($out_dir);
my $manifest_path = File::Spec->catfile($out_dir, "manifest.txt");
open(my $mf, '>:raw', $manifest_path) or die "Failed to write '$manifest_path': $!\n";
print $mf "# Extracted BMPs from TXL container\n";
print $mf "# source: $input\n";
print $mf "# columns: index file offset size width height bpp\n";

my $index = 0;
my $cursor = 0;
while (1) {
    my $pos = index($data, "BM", $cursor);
    last if $pos < 0;
    $cursor = $pos + 2;

    next if $pos + 54 > $size;

    my $file_size = unpack("V", substr($data, $pos + 2, 4));
    my $pixel_ofs = unpack("V", substr($data, $pos + 10, 4));
    my $dib_size  = unpack("V", substr($data, $pos + 14, 4));

    next if $file_size < 54;
    next if $file_size > ($size - $pos);
    next if $pixel_ofs >= $file_size;
    next if $dib_size < 12;

    my ($width, $height, $planes, $bpp);
    if ($dib_size >= 40) {
        $width  = unpack("l<", substr($data, $pos + 18, 4));
        $height = unpack("l<", substr($data, $pos + 22, 4));
        $planes = unpack("v",  substr($data, $pos + 26, 2));
        $bpp    = unpack("v",  substr($data, $pos + 28, 2));
    } elsif ($dib_size == 12) {
        $width  = unpack("v",  substr($data, $pos + 18, 2));
        $height = unpack("v",  substr($data, $pos + 20, 2));
        $planes = unpack("v",  substr($data, $pos + 22, 2));
        $bpp    = unpack("v",  substr($data, $pos + 24, 2));
    } else {
        next;
    }

    next if !defined($planes) || $planes != 1;
    next if !defined($bpp) || ($bpp != 4 && $bpp != 8 && $bpp != 16 && $bpp != 24 && $bpp != 32);
    next if !defined($width) || !defined($height);
    next if $width <= 0 || abs($height) <= 0 || $width > 8192 || abs($height) > 8192;

    my $file_name = sprintf("txl_%05d.bmp", $index);
    my $out_path = File::Spec->catfile($out_dir, $file_name);
    open(my $of, '>:raw', $out_path) or die "Failed to write '$out_path': $!\n";
    print $of substr($data, $pos, $file_size);
    close($of);

    print $mf join(" ",
                   $index,
                   $file_name,
                   $pos,
                   $file_size,
                   $width,
                   abs($height),
                   $bpp), "\n";

    $index++;
}

close($mf);
print "Extracted $index BMP files into '$out_dir'.\n";
print "Manifest: $manifest_path\n";
