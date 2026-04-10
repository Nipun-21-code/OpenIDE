# Define colors for output
GREEN='\033[0;32m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

echo -e "${BLUE}--- Terminal File Manager Setup ---${NC}"

# 1. Install dependencies
echo -e "Step 1: Checking and installing dependencies..."
sudo apt update
sudo apt install -y build-essential libncurses5-dev libncursesw5-dev

# 2. Compile the code
echo -e "Step 2: Compiling fm.c..."
if [ -f "fm.c" ]; then
    gcc fm.c -o fm -lncurses
    if [ $? -eq 0 ]; then
        echo -e "${GREEN}Compilation successful!${NC}"
    else
        echo "Error: Compilation failed."
        exit 1
    fi
else
    echo "Error: fm.c not found in the current directory."
    exit 1
fi

# 3. Set permissions
chmod +x fm

# 4. Run the file manager
echo -e "Step 3: Launching File Manager..."
sleep 1
./fm
