# Install requirements 
- wsl
- vscode
- usbipd

## in wsl
- docker

# Opening project
- Open vscode in wsl at .
- Reopen in devcontainer


# Debugger

connect debugger with target to windows USB 
in windows run ( with ?-? being the id of the debugger (cmsis-dap) ) (bind needs admin)
- usbipd list
- usbipd bind --busid ?-?
- usbipd attach --wsl --busid ?-?
after bind once only attach is required if hardware didnt change
after attach reload window to give container all usb devices 
