#!/usr/bin/env python3
# -*- coding: utf-8 -*-
import argparse
import re
import subprocess
import sys

def main():
    parser = argparse.ArgumentParser(description="Check for trailing spaces in Solvitaire output")
    parser.add_argument('--exe', required=True, help="Path to solvitaire executable")
    parser.add_argument('--gametype', required=True, help="Game type")
    parser.add_argument('--deal', required=True, help="Path to deal file")
    
    args = parser.parse_args()
    
    try:
        out = subprocess.check_output(
            [args.exe, '--type', args.gametype, args.deal]).decode('utf-8')
        # Check for space or tab followed by newline or end of string
        if re.search(r'[ \t](?:\n|\Z)', out, flags=(re.MULTILINE | re.DOTALL)):
            print("Error: Trailing spaces/tabs found in output")
            sys.exit(1)
        print("No trailing spaces found.")
    except Exception as e:
        print(f"Error running check: {e}")
        sys.exit(1)

if __name__ == "__main__":
    main()
