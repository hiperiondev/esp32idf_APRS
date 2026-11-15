import sys, asyncio, json, os, re
ICONS = os.environ.get('APRS_ICONS', os.path.join(os.path.dirname(os.path.abspath(__file__)), 'cache', 'icons'))
CHROME = os.environ.get('CHROME')
from playwright.async_api import async_playwright
from PIL import Image
PAGES = ['dashboard','station','msgchat','bulletins','objects','radio','msg','query','igate','bm','digi','tracker','wx','tlm','gps','telegram','winlink','logs','system','wireless','storage','about','symbol']
async def icon(route):
    fn = os.path.join(ICONS, route.request.url.rsplit('/', 1)[1])
    if os.path.exists(fn): await route.fulfill(path=fn, content_type='image/png')
    else: await route.fulfill(status=404, body='')
def squash(path):
    im = Image.open(path).convert('RGB')
    im.quantize(colors=128, method=Image.Quantize.MEDIANCUT, dither=Image.Dither.NONE).save(path, optimize=True)
async def main():
    port, outdir = sys.argv[1], sys.argv[2]
    os.makedirs(outdir, exist_ok=True)
    index = {}
    async with async_playwright() as p:
        b = await p.chromium.launch(executable_path=CHROME, args=['--no-sandbox']) if CHROME else await p.chromium.launch()
        ctx = await b.new_context(viewport={'width': 1280, 'height': 860})
        pg = await ctx.new_page()
        await pg.route('http://aprs.dprns.com/**', icon)
        for name in PAGES:
            print(name, flush=True)
            await pg.goto(f'http://127.0.0.1:{port}/{name}')
            await pg.wait_for_timeout(1800)
            await pg.evaluate("window.scrollTo(0,0)")
            f = f'{outdir}/{name}_top.png'
            await pg.screenshot(path=f); squash(f)
            index[f'{name}_top'] = (await pg.title()) + ' / ' + (await pg.locator('main h1').first.inner_text())
            fs = pg.locator('main.content fieldset:not(fieldset fieldset)')
            n = await fs.count(); k = 0
            for i in range(n):
                el = fs.nth(i)
                box = await el.bounding_box()
                if not box or box['height'] < 5 or box['height'] > 2400:
                    print('skip', name, i, box, flush=True); continue
                f = f'{outdir}/{name}_fs{k:02d}.png'
                for attempt in range(3):
                    try:
                        await el.screenshot(path=f, timeout=8000); break
                    except Exception as e:
                        print('retry', name, i, e.__class__.__name__); await pg.wait_for_timeout(500)
                squash(f)
                index[f'{name}_fs{k:02d}'] = (await el.locator('legend').first.inner_text()) if await el.locator('legend').count() else ''
                k += 1
            if k == 0 or name in ('system', 'wireless'):
                el = pg.locator('main.content')
                f = f'{outdir}/{name}_main.png'
                await el.screenshot(path=f); squash(f); index[f'{name}_main'] = 'main'
        # contextual help shown open
        await pg.goto(f'http://127.0.0.1:{port}/igate'); await pg.wait_for_timeout(800)
        h = pg.locator('main.content fieldset').first
        await pg.locator('main.content .hlp').first.focus(); await pg.wait_for_timeout(300)
        await pg.evaluate("window.scrollTo(0,0)"); bb = await h.bounding_box()
        await pg.screenshot(path=f'{outdir}/help_open.png', clip={'x': bb['x']-10, 'y': bb['y']-70, 'width': bb['width']+20, 'height': bb['height']+80}); squash(f'{outdir}/help_open.png')
        # mobile layout
        m = await b.new_context(viewport={'width': 390, 'height': 780}, device_scale_factor=1)
        mp = await m.new_page(); await mp.route('http://aprs.dprns.com/**', icon)
        await mp.goto(f'http://127.0.0.1:{port}/dashboard'); await mp.wait_for_timeout(1500)
        await mp.screenshot(path=f'{outdir}/mobile_dashboard.png'); squash(f'{outdir}/mobile_dashboard.png')
        await b.close()
    json.dump(index, open(f'{outdir}/index.json','w'), indent=1, ensure_ascii=False)
asyncio.run(main())
