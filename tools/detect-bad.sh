#!/bin/sh
# Scans Korean output for sequences that no correct translation contains and prints one line
# per hit: FILE:LINE, the pattern id and the matched span with a few characters around it.
#   tools/detect-bad.sh FILE...        (a file's first line is skipped when it starts with '#')
#   tools/detect-bad.sh -c FILE...     (hit counts per pattern instead)
#   tools/detect-bad.sh [-c] --source JA KO [JA KO]...
#                                      (source-aware checks on line-aligned pairs; see below)
# Patterns:
#   ending-honorific      a final ending followed by 군/씨/님/짱 (했다님, 있어요씨)
#   doubled-ending        two final endings in a row (그렇네요군요, 이에요입니다)
#   stem-eung             the interjection 응 right after a verb stem (하응, 않응)
#   particle-after-punct  a particle standing after , . ! ? … or ~ (…를, ! 와); not 가 주 (go) or 와? (come)
#   residue               Japanese script or engine markers left in the output (승용き, &AV@, (이)가)
# Source-aware patterns (--source): both files of a pair are cut at the same punctuation
# (「」『』（）、。！？…‥―), and when they give the same number of segments, each Japanese
# segment that reads as a sound (two to five kana, ー or ～ with ッ, not first, not starting with
# を) is checked in its Korean segment:
#   sound-function        a function word or particle as a word (인가 하, 하지만 하, 아는 은 은);
#                         not a stutter, a syllable that starts the next segment (こっ、この 이, 이)
# Exit: 0 without hits, 1 with hits, 2 invalid arguments.
set -eu
count=0 source=0
if [ "${1:-}" = -c ]; then count=1; shift; fi
if [ "${1:-}" = --source ]; then source=1; shift; fi
[ $# -gt 0 ] || { echo 'usage: tools/detect-bad.sh [-c] [--source JA KO ...] FILE...' >&2; exit 2; }
if [ $source = 1 ] && [ $(($# % 2)) -ne 0 ]; then
    echo 'usage: tools/detect-bad.sh [-c] --source JA KO [JA KO]...' >&2; exit 2
fi
for f in "$@"; do
    [ -f "$f" ] || { echo "no file $f" >&2; exit 2; }
done
if [ $source = 1 ]; then
    LC_ALL=C.UTF-8 exec perl -CSD -Mutf8 -e '
        my $count = shift @ARGV;
        my %hits = ("sound-function" => 0);
        my $cut = qr/[「」『』（）()、，,。.！!？?…‥―]+/;
        my $function = qr/(?:^|\s)(?:인가|하지만|그러나|그리고|그래서|그런데|또는|혹은|의|를|을|은|는|이|가|와|과|도|에|로|으로)(?=\s|$)/;
        while (@ARGV) {
            my ($ja, $ko) = splice(@ARGV, 0, 2);
            open(my $j, "<", $ja) or die "$ja: $!";
            open(my $k, "<", $ko) or die "$ko: $!";
            my $n = 0;
            while (defined(my $a = <$j>)) {
                my $b = <$k>;
                last unless defined $b;
                $n++;
                chomp($a, $b);
                next if $n == 1 && $a =~ /^#/;
                my @js = grep { $_ ne "" } map { s/^[\s　]+|[\s　]+$//gr } split($cut, $a);
                my @ks = grep { $_ ne "" } map { s/^[\s　]+|[\s　]+$//gr } split($cut, $b);
                next unless @js == @ks;
                for my $i (0 .. $#js) {
                    my ($s, $t) = ($js[$i], $ks[$i]);
                    next unless $s =~ /^[ぁ-ゖァ-ヶー～]{2,5}$/ && $s =~ /^[^をっッーぁぃぅぇぉゃゅょァィゥェォャュョ～]./ && $s =~ /[っッ]/;
                    next unless $t =~ $function;
                    next if $i < $#ks && length($t) == 1 && index($ks[$i + 1], $t) == 0;
                    $hits{"sound-function"}++;
                    print "$ko:$n\tsound-function\t$s\t$t\n" unless $count;
                }
            }
        }
        if ($count) { printf "%s\t%d\n", $_, $hits{$_} for sort keys %hits; }
        my $total = 0; $total += $_ for values %hits;
        exit($total ? 1 : 0);
    ' "$count" "$@"
fi
LC_ALL=C.UTF-8 perl -CSD -Mutf8 -e '
    my $count = shift @ARGV;
    my @patterns = (
        ["ending-honorific",
         qr/(?:[었았였했겠는한된]다|니다|[어아해네지세]요|이야|거야)(?:군|씨|님|짱)(?=$|[^가-힣]|[이은을의가는도과와에](?:$|[^가-힣]))/],
        ["doubled-ending",
         qr/(?:[어아해네지세]요|니다|죠)(?:군요|네요|이에요|예요|입니다|이다|어요|지요|습니다)(?![가-힣])|[었았였했겠는한]다(?:군요|이에요|예요|입니다|어요)(?![가-힣])/],
        ["stem-eung", qr/(?<![가-힣])(?:하|되|있|없|않|했|됐|봤|갔|왔)응(?![가-힣])/],
        ["particle-after-punct",
         qr/[,.!?…~](?: )?(?:(?:은|는|을|를|의|과|도|로|으로)(?=$|[\s.,!?」』…)])|가(?=$|[.,!?」』…]| (?!주))|와(?=$|[\s.,」』…])|에(?=$|[\s」』]))/],
        ["residue", qr/[ぁ-んァ-ヶ一-龥々]+|[&%\$][0-9A-Z][0-9A-Za-z]@|\((?:이|을|은|와|으|이라|과)\)/],
    );
    my %hits = map { $_->[0] => 0 } @patterns;
    while (my $line = <>) {
        chomp $line;
        if ($. == 1 && $line =~ /^#/) { next; }
        for my $p (@patterns) {
            while ($line =~ /$p->[1]/g) {
                my ($s, $e) = ($-[0], $+[0]);
                $hits{$p->[0]}++;
                next if $count;
                my $from = $s > 6 ? $s - 6 : 0;
                my $span = substr($line, $from, $e + 4 - $from);
                $span =~ s/\t/ /g;
                print "$ARGV:$.\t$p->[0]\t$span\n";
            }
        }
    } continue {
        close ARGV if eof;
    }
    if ($count) { printf "%s\t%d\n", $_->[0], $hits{$_->[0]} for @patterns; }
    my $total = 0; $total += $_ for values %hits;
    exit($total ? 1 : 0);
' "$count" "$@"
