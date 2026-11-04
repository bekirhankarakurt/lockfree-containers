# Demo recording for the README, rendered with charmbracelet/vhs:
#   cmake -S . -B build -DCMAKE_BUILD_TYPE=Release && cmake --build build -j
#   vhs docs/demo.tape
Output docs/demo.gif

Set Shell "bash"
Set FontSize 15
Set Width 1100
Set Height 680
Set Padding 16
Set Theme "Dracula"
Set TypingSpeed 35ms
Set Framerate 12

Hide
Type "export PS1='$ ' && clear"
Enter
Show

Type "./build/bench/lfc_bench --quick --section queue"
Sleep 300ms
Enter
Sleep 4s

Type "./build/bench/lfc_bench --quick --section latency"
Sleep 300ms
Enter
Sleep 4s
