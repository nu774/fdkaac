#!/bin/sh
pandoc -s -f markdown -t man README >fdkaac.1 && mv -f fdkaac.1 man/fdkaac.1
