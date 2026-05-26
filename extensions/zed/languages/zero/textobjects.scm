(function_declaration body: (block) @function.inside) @function.around
(shape_declaration body: (_) @class.inside) @class.around
(choice_declaration body: (_) @class.inside) @class.around
(enum_declaration body: (_) @class.inside) @class.around
(impl_declaration body: (_) @class.inside) @class.around
(line_comment)+ @comment.around
(block_comment) @comment.around
