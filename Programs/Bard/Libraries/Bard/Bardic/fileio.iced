fs   = require("fs")
file = require("file")

Filepath =
{
  absolute: (filepath) ->
    return file.path.abspath( filepath )
}

module.exports.Filepath = Filepath

