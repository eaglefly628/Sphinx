// Standalone viewer for the Sphinx satellite-ground surface data structure.
// Loads ../output/{surface_tile.json, ground.glb, albedo.png, splat_*_rgba.png,
// normal.png} and renders the georeferenced ground mesh with a layered-splat
// material so the result is inspectable without UE.
import * as THREE from 'three';
import { OrbitControls } from 'three/addons/controls/OrbitControls.js';
import { GLTFLoader } from 'three/addons/loaders/GLTFLoader.js';

const OUT = '../output/';
const errEl = document.getElementById('err');
const showErr = (m) => { errEl.textContent = String(m); console.error(m); };

const app = document.getElementById('app');
const renderer = new THREE.WebGLRenderer({ antialias: true });
renderer.setPixelRatio(Math.min(devicePixelRatio, 2));
renderer.outputColorSpace = THREE.LinearSRGBColorSpace; // show textures as authored
app.appendChild(renderer.domElement);

const scene = new THREE.Scene();
scene.background = new THREE.Color(0x101216);
const camera = new THREE.PerspectiveCamera(50, 1, 0.1, 5000);
camera.position.set(160, 160, 220);
const controls = new OrbitControls(camera, renderer.domElement);
controls.enableDamping = true;

function resize() {
  const w = innerWidth, h = innerHeight;
  renderer.setSize(w, h);
  camera.aspect = w / h;
  camera.updateProjectionMatrix();
}
addEventListener('resize', resize);
resize();

// ---- layered-splat shader ---------------------------------------------------
const vert = /* glsl */`
  varying vec2 vUv; varying vec3 vNormal;
  uniform float uRelief;
  void main(){
    vUv = uv;
    vec3 p = position; p.y *= uRelief;
    vNormal = normalize(normalMatrix * normal);
    gl_Position = projectionMatrix * modelViewMatrix * vec4(p,1.0);
  }`;
const frag = /* glsl */`
  precision highp float;
  varying vec2 vUv; varying vec3 vNormal;
  uniform sampler2D uAlbedo, uSplat0, uSplat1;
  uniform vec3 uColors[8];
  uniform vec3 uLightDir;
  uniform int uMode; uniform float uDetail;
  float hash(vec2 p){ return fract(sin(dot(p, vec2(127.1,311.7)))*43758.5453); }
  float noise(vec2 p){ vec2 i=floor(p), f=fract(p); f=f*f*(3.0-2.0*f);
    float a=hash(i), b=hash(i+vec2(1,0)), c=hash(i+vec2(0,1)), d=hash(i+vec2(1,1));
    return mix(mix(a,b,f.x), mix(c,d,f.x), f.y); }
  void main(){
    vec4 s0 = texture2D(uSplat0, vUv); vec4 s1 = texture2D(uSplat1, vUv);
    float w[8]; w[0]=s0.r; w[1]=s0.g; w[2]=s0.b; w[3]=s0.a;
    w[4]=s1.r; w[5]=s1.g; w[6]=s1.b; w[7]=s1.a;
    vec3 seg = vec3(0.0); float tot = 0.0;
    for(int i=0;i<8;i++){ seg += w[i]*uColors[i]; tot += w[i]; }
    seg /= max(tot, 1e-3);
    vec3 macro = texture2D(uAlbedo, vUv).rgb;
    float ndl = clamp(dot(normalize(vNormal), normalize(uLightDir)), 0.0, 1.0);
    float light = 0.4 + 0.95*ndl;
    vec3 color;
    if(uMode==0){ color = macro; }
    else if(uMode==2){ color = seg; }
    else {
      float n = noise(vUv*uDetail);                 // micro detail variation
      vec3 detailed = macro*(0.7 + 0.6*n);          // break up the flat photo
      color = mix(detailed, detailed*seg*1.7, 0.3); // nudge toward class tint
    }
    gl_FragColor = vec4(color*light, 1.0);
  }`;

let material, meshObj, objGroup, data;
const uniforms = {
  uAlbedo: { value: null }, uSplat0: { value: null }, uSplat1: { value: null },
  uColors: { value: Array.from({ length: 8 }, () => new THREE.Color(0x808080)) },
  uLightDir: { value: new THREE.Vector3(1, 1.2, 0.6).normalize() },
  uMode: { value: 1 }, uRelief: { value: 6.0 }, uDetail: { value: 120.0 },
};

const texLoader = new THREE.TextureLoader();
function loadTex(name) {
  return new Promise((res) => texLoader.load(OUT + name, (t) => {
    t.colorSpace = THREE.NoColorSpace; t.flipY = false;
    t.wrapS = t.wrapT = THREE.ClampToEdgeWrapping;
    res(t);
  }, undefined, () => res(null)));
}

async function init() {
  try {
    data = await (await fetch(OUT + 'surface_tile.json')).json();
    document.getElementById('subtitle').textContent =
      `${data.tile_id} · ${data.geo.size_m}m · ${data.source.kind} · UTM${data.geo.utm_zone}`;
    buildLegend(data.classes);
    for (let i = 0; i < data.classes.length && i < 8; i++) {
      const c = data.classes[i].color;
      uniforms.uColors.value[i] = new THREE.Color(c[0]/255, c[1]/255, c[2]/255);
    }
    const splatNames = data.raster.splat.paths;
    const [alb, sp0, sp1] = await Promise.all([
      loadTex(data.raster.albedo.path),
      loadTex(splatNames[0]),
      loadTex(splatNames[1] || splatNames[0]),
    ]);
    uniforms.uAlbedo.value = alb;
    uniforms.uSplat0.value = sp0;
    uniforms.uSplat1.value = sp1;

    material = new THREE.ShaderMaterial({
      uniforms, vertexShader: vert, fragmentShader: frag, side: THREE.DoubleSide,
    });

    const gltf = await new GLTFLoader().loadAsync(OUT + data.mesh.glb);
    gltf.scene.traverse((o) => { if (o.isMesh) { o.material = material; meshObj = o; } });
    scene.add(gltf.scene);

    addObjects(data);
    document.getElementById('stats').textContent =
      `${data.mesh.vertices} verts · ${data.mesh.triangles} tris · ` +
      `albedo ${data.raster.albedo.width}px (${data.raster.albedo.mpp.toFixed(2)} m/px) · ` +
      `${data.objects.count} objects`;
  } catch (e) { showErr('Load failed: ' + e.message + '\n(serve via http, not file://)'); }
}

function buildLegend(classes) {
  const el = document.getElementById('legend');
  el.innerHTML = '';
  classes.forEach((c) => {
    if (c.coverage < 0.003) return;
    const d = document.createElement('div');
    d.innerHTML = `<span class="sw" style="background:rgb(${c.color.join(',')})"></span>` +
      `${c.name} <span style="margin-left:auto;color:#7f8799">${(c.coverage*100).toFixed(1)}%</span>`;
    el.appendChild(d);
  });
}

function addObjects(data) {
  if (objGroup) scene.remove(objGroup);
  objGroup = new THREE.Group();
  const half = data.geo.size_m / 2;
  const geom = new THREE.SphereGeometry(0.7, 10, 8);
  const mat = new THREE.MeshBasicMaterial({ color: 0xff3bd0 });
  const items = (data.objects && data.objects.items) || [];
  const inst = new THREE.InstancedMesh(geom, mat, items.length);
  const m = new THREE.Matrix4();
  items.forEach((o, i) => {
    m.makeTranslation(o.local_e_m - half, 1.0, -(o.local_n_m - half));
    inst.setMatrixAt(i, m);
  });
  objGroup.add(inst);
  scene.add(objGroup);
}

// ---- UI ---------------------------------------------------------------------
document.querySelectorAll('#modes button').forEach((b) => b.onclick = () => {
  document.querySelectorAll('#modes button').forEach((x) => x.classList.remove('active'));
  b.classList.add('active');
  uniforms.uMode.value = parseInt(b.dataset.mode, 10);
});
const relief = document.getElementById('relief');
relief.oninput = () => { uniforms.uRelief.value = +relief.value;
  document.getElementById('rlabel').textContent = relief.value + '×'; };
const sun = document.getElementById('sun');
sun.oninput = () => {
  const a = relief; void a;
  const rad = THREE.MathUtils.degToRad(+sun.value);
  uniforms.uLightDir.value.set(Math.cos(rad), 1.1, Math.sin(rad)).normalize();
  document.getElementById('slabel').textContent = sun.value + '°';
};
document.getElementById('wire').onclick = (e) => {
  e.target.classList.toggle('active');
  if (material) material.wireframe = !material.wireframe;
};
document.getElementById('objs').onclick = (e) => {
  e.target.classList.toggle('active');
  if (objGroup) objGroup.visible = !objGroup.visible;
};

// ---- light helper + loop ----------------------------------------------------
scene.add(new THREE.AmbientLight(0xffffff, 0.3));
const grid = new THREE.GridHelper(256, 16, 0x334, 0x223);
grid.position.y = -0.01; scene.add(grid);

function animate() {
  requestAnimationFrame(animate);
  controls.update();
  renderer.render(scene, camera);
}
animate();
init();
