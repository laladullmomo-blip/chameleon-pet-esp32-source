// Small code-native pixel icons. No external icon/font dependencies.
const palettes={heart:['#f17083','#a94763','#ffe1d2'],food:['#e6a35d','#8d633f','#fff0af'],energy:['#f4cf65','#b88d3b','#fff4bd'],mood:['#e3c361','#a98638','#fff6c9'],clean:['#7dbcb9','#44888d','#ddf7df'],cricket:['#6b9846','#3f6134','#c9df86'],mealworm:['#c89961','#8a6142','#f8d8a4'],medicine:['#8cbbd4','#527d9b','#e3f7e9'],soap:['#acd2be','#679b8d','#eefce8'],ball:['#e8a855','#bf6554','#83b6c8'],bag:['#d5996b','#8c6145','#f9d986'],camera:['#879ab3','#52677c','#e9eedb'],settings:['#8aaa94','#567e69','#e8f0d5'],book:['#91ae78','#5d7d54','#f5f3cf'],about:['#a0a3d1','#6c6c9a','#f5f4e9'],sleep:['#b3b6d6','#727995','#f4f4dc'],walk:['#89b579','#4f7846','#dfeeae'],jump:['#a4bd74','#648643','#f0eab3'],blink:['#bed585','#658c4e','#f0f5d5'],stretch:['#c9b184','#8e744e','#fcf0bf'],wave:['#dfa66f','#a27145','#fff0ba'],dizzy:['#d4bd63','#9d8438','#fff1a6'],angry:['#ce9479','#8d5b4f','#fbe2bd'],sick:['#7db7c6','#4b8592','#e1f5e8'],pet:['#dfb188','#a87758','#fff1d6'],coin:['#d4ad54','#947336','#fff0a5']};
palettes.cockroach=['#ad7750','#704b35','#deb784'];palettes.fly=['#83949a','#465f66','#d3e8df'];
const shapes={
 cockroach:['M6 3h4v1h2v8h-2v2H6v-2H4V4h2z','M7 4h2v9H7z M3 5h1v2H3z M1 7h2v1H1z M3 10h1v2H3z M2 12h1v2H2z M12 5h1v2h-1z M13 7h2v1h-2z M12 10h1v2h-1z M13 12h1v2h-1z M5 1h1v2H5z M10 1h1v2h-1z','M6 5h1v4H6z M9 5h1v4H9z'],
 fly:['M6 5h4v8H6z M7 3h3v3H7z','M6 11h4v2H6z M5 12h1v2H5z M10 12h1v2h-1z M7 3h1v1H7z','M2 3h3v1h1v4H3V7H1V4h1z M11 3h3v1h1v3h-1v1h-4V4h1z'],
 heart:['M2 4h4v2h4V4h4v6h-2v2h-2v2H6v-2H4v-2H2z','M2 4h4v1H3v5H2z M6 13h4v1H6z','M3 5h2v2H3z'],
 food:['M2 8h12v5H2z M4 5h8v3H4z M6 3h4v2H6z','M1 12h14v2H1z M5 6h2v1H5z M10 8h2v1h-2z','M3 9h10v2H3z'],
 energy:['M8 1h5L9 6h4l-7 9 1-6H3z','M6 8h3l-3 5z','M9 2h2L7 7H5z'],
 mood:['M4 2h8v1h2v2h1v7h-1v2h-2v1H4v-1H2v-2H1V5h1V3h2z','M4 6h2v2H4z M10 6h2v2h-2z M5 10h1v1h4v-1h1v2H5z','M4 3h5v1H4z'],
 clean:['M4 4h8v1h2v7h-2v2H4v-2H2V5h2z','M3 11h10v1H3z M7 1h2v2H7z M1 2h2v2H1z M13 1h2v2h-2z','M5 6h4v2H5z M11 7h1v1h-1z'],
 cricket:['M5 5h5v7H5z M9 3h3v4H9z','M3 4h2v2H3z M2 3h1v1H2z M10 1h1v2h-1z M12 2h2v1h-2z M3 9h2v1H3z M2 10h1v3H2z M4 13h2v1H4z M10 9h2v1h-2z M12 10h1v3h-1z M13 13h2v1h-2z','M10 4h1v1h-1z M6 6h2v3H6z'],
 mealworm:['M2 9h2V7h2V5h2V4h3v2h2v3h-2v2H9v2H6v1H3v-1H2z','M3 11h2v1H3z M6 9h1v2H6z M8 6h1v2H8z M10 4h1v1h-1z','M3 9h1v1H3z M6 7h1v1H6z'],
 medicine:['M6 2h4v2h2v10H4V4h2z','M5 1h6v2H5z M4 12h8v2H4z','M7 6h2v2h2v2H9v2H7v-2H5V8h2z'],
 soap:['M3 7h9v1h2v5h-2v1H3v-1H1V9h2z','M2 12h11v1H2z M5 3h2v2H5z M9 1h3v3H9z','M4 9h5v2H4z M10 8h2v1h-2z'],
 ball:['M4 2h8v1h2v2h1v7h-1v2h-2v1H4v-1H2v-2H1V5h1V3h2z','M3 4h4v3H3z M8 9h5v3H8z','M7 3h3v5H7z M3 9h3v3H3z'],
 bag:['M4 5h8v1h2v9H2V6h2z','M5 2h6v3H9V4H7v1H5z M2 13h12v2H2z','M5 8h6v4H5z M7 9h2v1H7z'],
 camera:['M2 5h12v9H2z M5 3h6v2H5z','M5 7h6v5H5z M13 6h1v1h-1z','M6 8h4v3H6z M3 6h2v1H3z'],
 settings:['M6 1h4v3h3v2h2v4h-3v3h-2v2H6v-3H3v-2H1V6h3V3h2z','M5 5h6v6H5z','M6 6h4v4H6z'],
 book:['M1 3h6v1h2V3h6v11H9v1H7v-1H1z','M7 4h2v10H7z','M2 5h4v1H2z M2 8h4v1H2z M10 5h4v1h-4z M10 8h4v1h-4z'],
 about:['M4 2h8v1h2v2h1v7h-1v2h-2v1H4v-1H2v-2H1V5h1V3h2z','M5 13h6v1H5z','M7 4h2v2H7z M6 7h3v5H6v-1h1V8H6z'],
 sleep:['M8 2h5v2h-2v2H9v2H7v2h6v2H4v-2h2V8h2V6h2V4H8z','M3 11h2v1H3z M11 14h2v1h-2z','M1 3h2v2H1z M13 2h1v1h-1z'],
 walk:['M3 8h4v3h2v3H2v-2h1z M9 2h4v3h2v3H8V6h1z','M2 13h7v1H2z M8 7h7v1H8z','M4 8h2v2H4z M10 2h2v2h-2z'],
 jump:['M6 1h4v2h2v2h-2v4H6V5H4V3h2z M3 12h3v2H3z M10 12h3v2h-3z','M5 10h6v1H5z M2 14h12v1H2z','M7 2h2v5H7z'],
 blink:['M3 5h10v1h2v4h-2v1H3v-1H1V6h2z','M2 7h12v2H2z','M6 6h4v4H6z'],
 stretch:['M6 3h4v4h4v2h-4v5H6V9H2V7h4z','M1 5h2v3H1z M13 5h2v3h-2z M5 14h6v1H5z','M7 4h2v2H7z'],
 wave:['M5 4h2V2h2v3h2V3h2v7h-2v4H5v-3H3V6h2z','M3 9h2v2H3z M5 13h6v1H5z M1 2h2v1H1z M13 1h2v1h-2z','M7 5h2v4H7z'],
 dizzy:['M7 1h2v3h3v2h3v2h-3v3H9v4H7v-4H4V8H1V6h3V4h3z','M5 6h6v4H5z','M7 6h2v2H7z M2 12h2v2H2z M12 12h2v2h-2z'],
 angry:['M4 2h8v1h2v2h1v7h-1v2h-2v1H4v-1H2v-2H1V5h1V3h2z','M3 5h2v1h2v2H5V7H3z M9 6h2V5h2v2h-2v1H9z M6 11h4v1H6z','M4 3h5v1H4z'],
 sick:['M3 6h10v8H3z M5 4h6v2H5z','M2 12h12v2H2z','M7 7h2v2h2v2H9v2H7v-2H5V9h2z'],
 pet:['M3 6h2V3h2v3h2V2h2v5h2V4h2v7h-2v3H5v-3H3z','M3 10h2v1H3z M5 13h8v1H5z','M6 7h2v3H6z'],
 coin:['M4 2h8v1h2v2h1v7h-1v2h-2v1H4v-1H2v-2H1V5h1V3h2z','M4 13h8v1H4z','M7 4h2v1h2v2H7v1h4v3H9v1H7v-1H5V9h2v1h2V9H5V5h2z']
};
export function icon(name, label=''){if(name==='happy')name='mood';const p=palettes[name]||palettes.about,s=shapes[name]||shapes.about;return `<svg viewBox="0 0 16 16" shape-rendering="crispEdges" aria-hidden="${!label}" ${label?`role="img" aria-label="${label}"`:''}>${s.map((d,i)=>`<path d="${d}" fill="${p[i]}"></path>`).join('')}</svg>`;}
