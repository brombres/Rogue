all:
	cd ../Libraries/Standard && python ../../DocGen/makedoc.py *.rogue | pbcopy

rogue_unused:
	mkdir -p Build
	roguec DocGen.rogue --output=Build --execute
