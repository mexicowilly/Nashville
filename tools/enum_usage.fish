#!/usr/bin/env fish

# Usage: check_enum_usage header.h source.cpp class_name enum_name

set header $argv[1]
set source $argv[2]
set class_name $argv[3]
set enum_name $argv[4]

# Extract enum values from header
set enum_values (sed -n '/enum class $enum_name/,/};/p' $header | \
    sed 's,//.*,,g; s/[*][^*]*[*]//g; s/enum class[^{]*{//g; s/}//g; s/=.*//g; s/,/\n/g' | \
    sed 's/^[[:space:]]*//; s/[[:space:]]*$//' | grep -E '^[A-Za-z_][A-Za-z0-9_]*$')

# Create temporary file with constructor body removed
set tmp (mktemp)

awk -v cls="$class_name" '
BEGIN { state=0; depth=0 }
{
    line=$0
    # Step 1: detect constructor signature
    if (state==0 && line ~ cls "::" cls "[[:space:]]*\\(") {
        state=1
    }

    # Step 2: wait for opening brace
    if (state==1) {
        if (line ~ /{/) {
            state=2
            depth=1
            tmp_line=line
            opens=gsub(/{/, "{", tmp_line)
            closes=gsub(/}/, "}", tmp_line)
            depth += (opens-1)
            depth -= closes
            next
        } else {
            next
        }
    }

    # Step 3: inside constructor
    if (state==2) {
        tmp_line=line
        opens=gsub(/{/, "{", tmp_line)
        closes=gsub(/}/, "}", tmp_line)
        depth += opens
        depth -= closes
        if (depth <=0) { state=0; depth=0 }
        next
    }

    # Outside constructor: keep the line
    print line
}
' $source > $tmp

echo "Checking enum usage outside constructor..."

set unused 0

for val in $enum_values
    set pattern "$enum_name::$val"

    if not grep -q -- "$pattern" $tmp
        echo "Unused outside constructor: $val"
        set unused 1
    end
end

rm $tmp

if test $unused -eq 0
    echo "All enum values are used outside constructor ✅"
else
    echo
    echo "Some enum values are unused outside constructor ❌"
end
