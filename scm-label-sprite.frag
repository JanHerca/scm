#version 120

uniform sampler2D icons;

void main()
{
    vec2 c = (gl_PointCoord + gl_TexCoord[0].xy) * vec2(0.25, 1.00);

    gl_FragColor = vec4(gl_Color.rgb, gl_Color.a * texture2D(icons, c).r);
}
