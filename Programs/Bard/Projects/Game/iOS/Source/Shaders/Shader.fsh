//
//  Shader.fsh
//  Bard Dragon-iOS
//
//  Created by Abe Pralle on 11/6/13.
//  Copyright (c) 2013 Plasmaworks. All rights reserved.
//

varying lowp vec4 colorVarying;

void main()
{
    gl_FragColor = colorVarying;
}
