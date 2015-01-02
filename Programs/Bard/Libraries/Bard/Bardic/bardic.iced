require("./bard-string-augment")
BCC = require("./bcc")

try
  parser = new BCC.Parser()
  parser.include( "Test.bard" )
  program = parser.parse_program()
  program.resolve()

catch err
  console.log err.toString()
