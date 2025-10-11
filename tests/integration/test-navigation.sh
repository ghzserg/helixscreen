#!/bin/bash
# Test navigation by running the app and taking screenshots
# Press keys 0-4 to switch panels, then 's' to screenshot

echo "Testing navigation system..."
echo "This will run the app for 10 seconds and capture screenshots"
echo ""
echo "The test will:"
echo "  1. Start on home panel (icon 0 should be red)"
echo "  2. Take screenshot"
echo "  3. Exit"
echo ""
echo "You should manually test clicking the icons in the UI!"
echo ""

# Run app for 10 seconds
timeout 10 ./build/bin/guppy-ui-proto 2>&1 | grep -E "USER|ERROR|Active panel|Switched to"

echo ""
echo "Check latest screenshot at:"
ls -lt /tmp/ui-screenshot-*.png 2>/dev/null | head -1 | awk '{print $NF}'
