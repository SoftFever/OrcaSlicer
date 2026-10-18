const colors = require('./ui/theme/colors')

/** @type {import('tailwindcss').Config} */
module.exports = {
  content: ['./app/**/*.{ts,tsx}', './ui/**/*.{ts,tsx}'],
  presets: [require('nativewind/preset')],
  darkMode: 'class',
  theme: {
    extend: {
      // A single warm accent to break the neutral palette; the app leads with
      // near-black primary actions and reserves this teal for status, links
      // and selected states.
      colors: {
        accent: colors.accent,
      },
      // The type scale. Every Text picks one of these instead of a pixel size, so
      // the app has twelve sizes and one place to change them. Names follow iOS.
      fontSize: {
        caption: ['11px', { lineHeight: '14px' }],
        label: ['12px', { lineHeight: '16px' }],
        footnote: ['13px', { lineHeight: '18px' }],
        callout: ['14px', { lineHeight: '19px' }],
        subhead: ['15px', { lineHeight: '20px' }],
        headline: ['16px', { lineHeight: '21px' }],
        body: ['17px', { lineHeight: '22px' }],
        lead: ['18px', { lineHeight: '23px' }],
        title3: ['20px', { lineHeight: '25px' }],
        title2: ['22px', { lineHeight: '28px' }],
        title1: ['28px', { lineHeight: '34px' }],
        display: ['36px', { lineHeight: '42px' }],
      },
      letterSpacing: {
        caps: colors.capsTracking,
      },
      borderRadius: {
        card: '24px',
        control: '16px',
      },
      // Softer defaults: buttons and cards read as slightly floating on
      // a warm off-white / true-dark background rather than iOS glass.
      boxShadow: {
        soft: '0 1px 2px rgba(15, 23, 42, 0.04), 0 1px 3px rgba(15, 23, 42, 0.06)',
        raised: '0 4px 12px rgba(15, 23, 42, 0.06), 0 2px 4px rgba(15, 23, 42, 0.04)',
      },
    },
  },
}
