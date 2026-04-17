-- currently only works in ubuntu. PLEASE USE WSL TO TEST VIA WINDOWS

//OpenFM// is a simple, lightweight terminal file manager written purely in C. the c file can be downloaded anywhere on the system and shall be compiled in the directory with gcc OpenFM.c -lncurses -o openfm ; (make sure ncurses is installed on the system). After which it can be made a system binary for ease of use. 
OpenFM is NOT a toy project, but is a real tool allowing easy deletion and creation of files, folders, ease in navigating directories and editting of files using micro (if present on the system, otherwise defaulting to nano) all from within the terminal session. One can also configure other terminal editors with OpenFM in the code. this helps use the terminal effectively as a holistic IDE. 
commands/features:

scrolling with mouse works, and arrows keys can be use for navigation up and down the list. 
enter enters directory or opens file in micro. 
backspace returns to the parent directory of terminal session. 
user can choose (..) at the top of list to enter paarent directory of directory being viewed. 
Ctrl + D : deletes files or directory. (it uses sudo rm -rf which is  powerful command and can delete any and all system files if so chosen. be careful)
pressing the "/" button opens fm search. the top rewsult can be opened, entered with enter.

...

//Neotex// is at the initial stage of development. it is a simple program using gap buffer and dynamic memory allocation to operate 
as a functioning terminal based text editor. It is basic, and has very little overhead. 
It can create files with different extensions, open them f0r editting and has autoindentation. It is NOT an IDE itself, and has no syntax highlighting yet. 

This is a small hobby project with the main aim being, understanding memory allocation and file manipulation with C. 

Neotex operates using commands f0r cursor positioning, line deletion, saving files and exitting. 

:m L --> moves cursor to the start or line L
:d L --> deletes line L
:d *L --> deletes line L and everything afterwards. 
:d *L M* deletes everything between line L and M (inclusive).
:w --> saves the program without without editting.
ESVA --> saves the projects and exits neotex.
