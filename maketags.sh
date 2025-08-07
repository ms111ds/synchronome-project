#!/bin/bash

find -name .git -prune -o -print | grep -e "\.h" -e "\.c" | grep -v test | xargs etags