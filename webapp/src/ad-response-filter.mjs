const AD_KEYS = [
  'adBreakHeartbeatParams',
  'adBreakParams',
  'adPlacements',
  'adSlots',
  'adSignalsInfo',
  'adVideoId',
  'playerAds'
];

const REEL_AD_VIDEO_TYPE = 'REEL_VIDEO_TYPE_AD';

export function stripYouTubeAds(value, depth = 0) {
  if (!value || typeof value !== 'object' || depth > 8) return false;

  let changed = false;
  AD_KEYS.forEach((key) => {
    if (Object.prototype.hasOwnProperty.call(value, key)) {
      value[key] = Array.isArray(value[key]) ? [] : null;
      changed = true;
    }
  });

  Object.keys(value).forEach((key) => {
    const child = value[key];
    if (key === 'tvMastheadRenderer') {
      delete value[key];
      changed = true;
      return;
    }
    if (child && typeof child === 'object') {
      changed = stripYouTubeAds(child, depth + 1) || changed;
    }
  });

  return changed;
}

function isAdditionalAdEntry(value) {
  if (!value || typeof value !== 'object') return false;

  if (Object.prototype.hasOwnProperty.call(value, 'adSlotRenderer') ||
      Object.prototype.hasOwnProperty.call(value, 'tvMastheadRenderer') ||
      value.richItemRenderer?.content?.adSlotRenderer ||
      value.tileRenderer?.content?.adSlotRenderer) {
    return true;
  }

  const reelEndpoints = [
    value.command?.reelWatchEndpoint,
    value.onSelectCommand?.reelWatchEndpoint,
    value.navigationEndpoint?.reelWatchEndpoint,
    value.reelItemRenderer?.navigationEndpoint?.reelWatchEndpoint,
    value.tileRenderer?.onSelectCommand?.reelWatchEndpoint
  ];

  return reelEndpoints.some((endpoint) => {
    const isAd = endpoint?.adClientParams?.isAd;

    return (
      isAd === true ||
      isAd === 'true' ||
      endpoint?.videoType === REEL_AD_VIDEO_TYPE
    );
  });
}

// YouTube changes the nesting of browse and Shorts responses frequently. Remove
// the matching renderer wherever it occurs instead of relying on one response path.
export function stripAdditionalYouTubeAds(value, depth = 0) {
  if (!value || typeof value !== 'object' || depth > 32) return false;

  let changed = false;

  if (Array.isArray(value)) {
    for (let index = value.length - 1; index >= 0; index -= 1) {
      if (isAdditionalAdEntry(value[index])) {
        value.splice(index, 1);
        changed = true;
      } else {
        changed = stripAdditionalYouTubeAds(value[index], depth + 1) || changed;
      }
    }
    return changed;
  }

  Object.keys(value).forEach((key) => {
    if (key === 'adSlotRenderer') {
      delete value[key];
      changed = true;
      return;
    }

    changed = stripAdditionalYouTubeAds(value[key], depth + 1) || changed;
  });

  return changed;
}

