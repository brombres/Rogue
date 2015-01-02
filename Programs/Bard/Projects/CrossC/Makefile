BARD_SRC = \
  XCC.bard \
  XCError.bard    \
  Keywords.bard     \
  Preprocessor.bard \
  Token.bard \
  Tokenizer.bard

#  Alias.bard        \
#  BCWriter.bard     \
#  CmdQuery.bard     \
#  Parser.bard       \
#  TargetRules.bard  \
#  TokenReader.bard  \
#  Analyzer.bard     \
#  Cyber.bard        \
#  Local.bard        \
#  Preprocessor.bard \
#  TaskVisitor.bard  \
#  Type.bard         \
#  Attributes.bard   \
#  CPPWriter.bard    \
#  CyberPlugin.bard  \
#  Method.bard       \
#  Property.bard     \
#  Template.bard     \
#  Types.bard        \
#  Augment.bard      \
#  Cmd.bard          \
#  JSWriter.bard     \
#  Op.bard           \
#  Scanner.bard      \
#  Token.bard        \
#  Visitor.bard 

all: XCC.bc run

run:
	../../Programs/Mac/bard XCC.bc Test.xc

XCC.bc: $(BARD_SRC)
	../../Programs/Mac/bard ../../Programs/BCC.bc --source-folders=../../Libraries/Bard XCC.bard

