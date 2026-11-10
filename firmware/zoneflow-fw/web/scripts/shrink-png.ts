// Shrinks the documentation screenshots (256-colour quantisation with
// ImageMagick, about a third of the size). Skipped when `magick` is missing.
import { execFileSync } from 'node:child_process';
import { readdirSync } from 'node:fs';
import { join } from 'node:path';
import { fileURLToPath } from 'node:url';

const dir = fileURLToPath(new URL('../../../../docs/firmware/img/webui', import.meta.url));
try {
  execFileSync('magick', ['-version'], { stdio: 'ignore' });
} catch {
  console.log('magick not found; screenshots left as they are');
  process.exit(0);
}
for (const f of readdirSync(dir).filter((n) => n.endsWith('.png'))) {
  const p = join(dir, f);
  execFileSync('magick', [p, '-colors', '256', '-define', 'png:compression-level=9', '-strip', p]);
}
console.log(`shrunk screenshots in ${dir}`);
