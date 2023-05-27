#!/bin/sh

#  OrcaSlicer gettext
#  Created by SoftFever on 27/5/23.
#
xgettext --keyword=L --keyword=_L --keyword=_u8L --keyword=L_CONTEXT:1,2c --keyword=_L_PLURAL:1,2 --add-comments=TRN --from-code=UTF-8 --no-location --debug --boost -f ./bbl/i18n/list.txt -o ./bbl/i18n/OrcaSlicer.pot
./build_arm64/src/hints/Release/hintsToPot.app/Contents/MacOS/hintsToPot ./resources ./bbl/i18n


echo $PWD
pot_file="./bbl/i18n/OrcaSlicer.pot"
for dir in ./bbl/i18n/*/
do
    dir=${dir%*/}      # remove the trailing "/"
    lang=${dir##*/}    # extract the language identifier

    if [ -f "$dir/OrcaSlicer_${lang}.po" ]; then
        msgmerge -N -o $dir/OrcaSlicer_${lang}.po $dir/OrcaSlicer_${lang}.po $pot_file
        msgfmt --check-format -o ./resources/i18n/${lang}/OrcaSlicer.mo $dir/OrcaSlicer_${lang}.po
    fi
done
