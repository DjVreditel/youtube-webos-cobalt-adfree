import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';
import vm from 'node:vm';
import test from 'node:test';
import * as filters from '../src/shorts-response-filter.mjs';
import * as adFilters from '../src/ad-response-filter.mjs';
import { stripSponsoredQrCodePopups } from '../src/sponsored-qr-code-block.mjs';

const source = readFileSync(new URL('../src/adblock-preload.js', import.meta.url), 'utf8');
const normal = { tileRenderer: { onSelectCommand: { watchEndpoint: { videoId: 'normal' } } } };
const shortTile = { tileRenderer: { onSelectCommand: { reelWatchEndpoint: {
  overlay: { reelPlayerOverlayRenderer: { style: 'REEL_PLAYER_OVERLAY_STYLE_SHORTS' } }
} } } };

function parser(enabled = false, downstream = false) {
  const context = vm.createContext({
    ...filters,
    ...adFilters,
    stripSponsoredQrCodePopups,
    configRead: (key) => key === 'enableShorts' ? enabled : key === 'enableAdBlock',
    document: { querySelector: () => null },
    window: {},
    console: { info() {}, log() {}, warn() {}, error() {} }
  });
  vm.runInContext(source.slice(source.indexOf('if (!window.__ytafPreloadExecuted)')), context);
  if (downstream) {
    vm.runInContext(`
      const previousParse = JSON.parse;
      JSON.parse = function () { return previousParse.apply(this, arguments); };
    `, context);
  }
  return (value) => {
    context.input = JSON.stringify(value);
    return JSON.parse(vm.runInContext('JSON.stringify(JSON.parse(input))', context));
  };
}

test('preload filters paginated feed responses through a later parser hook', () => {
  const response = { continuationContents: { sectionListContinuation: { contents: [
    { shelfRenderer: { tvhtml5ShelfRendererType: 'TVHTML5_SHELF_RENDERER_TYPE_SHORTS' } },
    normal
  ] } } };
  const filtered = parser(false, true)(response);
  assert.deepEqual(filtered.continuationContents.sectionListContinuation.contents, [normal]);
});

test('preload filters Shorts tiles and direct reel items while keeping ordinary videos', () => {
  const response = { contents: { items: [normal, shortTile, { reelItemRenderer: { videoId: 'short' } }] } };
  assert.deepEqual(parser()(response).contents.items, [normal]);
  assert.deepEqual(parser(true)(response), response);
});

test('preload filters continuation commands without deleting mixed shelves', () => {
  const response = { onResponseReceivedActions: [{ appendContinuationItemsAction: {
    continuationItems: [{ shelfRenderer: { content: { horizontalListRenderer: {
      items: [normal, shortTile]
    } } } }]
  } }] };
  const filtered = parser()(response);
  assert.deepEqual(filtered.onResponseReceivedActions[0].appendContinuationItemsAction
    .continuationItems[0].shelfRenderer.content.horizontalListRenderer.items, [normal]);
});

test('player responses and unrelated JSON are preserved', () => {
  const parse = parser();
  for (const response of [
    { videoDetails: { videoId: 'playing' }, contents: { items: [shortTile] } },
    { settings: { items: [shortTile] } }
  ]) assert.deepEqual(parse(response), response);
  assert.equal(filters.isShortsPath({}), false);
});


test('preload removes homepage ad slots before browse content is consumed', () => {
  const response = { contents: { items: [
    normal,
    { adSlotRenderer: { slotId: 'home-ad' } },
    { richItemRenderer: { content: { adSlotRenderer: { slotId: 'nested' } } } },
    { tvMastheadRenderer: { title: 'advertisement' } }
  ] } };
  const result = parser(true, true)(response);
  assert.deepEqual(result.contents.items[0], normal);
  assert.deepEqual(result.contents.items, [normal]);
});

test('preload removes video ad fields and reel ad entries', () => {
  const response = { videoDetails: { videoId: 'normal' }, adPlacements: [{ ad: 1 }],
    playerAds: [{ ad: 2 }], contents: { items: [normal, {
      reelItemRenderer: { navigationEndpoint: { reelWatchEndpoint: {
        videoType: 'REEL_VIDEO_TYPE_AD'
      } } }
    }] } };
  const result = parser(true)(response);
  assert.deepEqual(result.adPlacements, []);
  assert.deepEqual(result.playerAds, []);
  assert.deepEqual(result.contents.items, [normal]);
});
