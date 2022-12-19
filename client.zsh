#!/usr/bin/env zsh
# mostly written by ChatGPT
ADDRESS="192.168.1.22"
PORT="3333"

# Known bug: it is impossible to enter a value equal to 10, eg: 10:00 is invalid,
# and so is 08:10
# No solution has been found

DEBUG=0


debug () {
    if [[ $DEBUG == 0 ]]; then
        return
    fi
    print $@
}

if [[ "$1" == "-h" ]] || [[ "$1" == "--help" ]]; then
  echo "Usage: $0 [0/1] [hh:mm] [hh:mm]"
echo ""
echo "  [0/1]      Select morning or evening period"
echo "  [hh:mm]    Start of the period between 00:00 and 23:59"
echo "  [hh:mm]    End of the period between 00:00 and 23:59"

  exit 0
fi

# Check if the correct number of arguments was provided
if [[ $# -ne 3 ]]; then
  echo "Error: script requires 3 arguments" >&2
  exit 1
fi



# Set up variables for the first argument and the two remaining arguments
arg1=$1
arg2=$2
arg3=$3

# Check that the first argument is either 0 or 1
if [[ "$arg1" -ne 0 ]] && [[ "$arg1" -ne 1 ]]; then
  echo "Error: first argument must be 0 or 1" >&2
  exit 1
fi

# we transform the value into its ascii value in order to send the integer to the server
# and not a string
msg=`printf "\x$(printf %x $arg1)"`

hours=($arg2 $arg3)
for elt in $hours; do
    # Split the second and third arguments into hour and minutes
    IFS=: read -r hour minute <<< "$elt"
    debug "hour" $hour "minute" $minute

    # Check that the hour is between 00 and 23
    if [[ "$hour" -lt 0 ]] || [[ "$hour" -gt 23 ]]; then
      echo "Error: invalid hour: ${elt}" >&2
      exit 1
    fi

    # Check that the minutes are between 00 and 59
    if [[ "$minute" -lt 0 ]] || [[ "$minute" -gt 59 ]]; then
      echo "Error: invalid minute: ${elt}" >&2
      exit 1
    fi

    # Convert the hour and minute to their ASCII values
    ascii_hour=`printf "\x$(printf %x $hour)"`
    ascii_minute=`printf "\x$(printf %x $minute)"`
    debug "length ascii: hour " ${#ascii_hour} "minute" ${#ascii_minute}
    msg+=${ascii_hour}${ascii_minute}
done

print Msg length: ${#msg}

# Send the arguments to the UDP server on port 3333, with no spaces between them
try=1
while [[ $try != 0 ]]; do
    print Try: $try...
    res=$(echo -n "$msg" | nc -w1 -u $ADDRESS $PORT)  # -e-> interpret backslash
    debug $res
    if [[ $res == *"invalid"* ]]; then
        try=0
        print "Invalid message. Please check"
    elif [[ $res == $msg ]]; then
        try=0
    else
        ((try+=1))
    fi
done
exit 0
