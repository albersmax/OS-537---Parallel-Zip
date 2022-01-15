#! /bin/bash

if ! [[ -x pzip ]]; then
    echo "pzip executable does not exist"
    exit 1
fi

# Copy the tests once
if ! [[ -d tests ]]; then
	cp ~cs537-1/tests/p3a/tests.tar.gz .
	tar -xvzf tests.tar.gz
	#cp -r ~cs537-1/tests/p3a/tests .
fi

# Copy and extract tar locally once
if ! [[ -d /nobackup/p3a-data ]]; then
	cp ~cs537-1/tests/p3a/p3a-data.tar.gz .
	tar -xvzf p3a-data.tar.gz -C /nobackup/
fi

/p/course/cs537-remzi/tests/tester/run-tests.sh $*

