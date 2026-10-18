import { describe, expect, it } from 'vitest'

import { HELP_TOPICS, helpTopic, imageKeys } from './content'

describe('help content', () => {
  it('has unique topic ids that resolve', () => {
    const ids = HELP_TOPICS.map((t) => t.id)
    expect(new Set(ids).size).toBe(ids.length)
    for (const id of ids) expect(helpTopic(id)?.id).toBe(id)
    expect(helpTopic('nope')).toBeUndefined()
  })

  it('names images under their own topic and never reuses one', () => {
    for (const topic of HELP_TOPICS) {
      const keys = imageKeys(topic)
      expect(new Set(keys).size).toBe(keys.length)
      for (const key of keys) {
        expect(key.startsWith(`${topic.id}/`)).toBe(true)
        // nn-platform-slug, the capture naming the docs generator relies on
        expect(key.slice(topic.id.length + 1)).toMatch(/^\d{2}-(desktop|ios)-[a-z0-9-]+$/)
      }
    }
  })

  it('gives every step a title and a body', () => {
    for (const topic of HELP_TOPICS)
      for (const section of topic.sections)
        for (const step of section.steps) {
          expect(step.title.length).toBeGreaterThan(0)
          expect(step.body.length).toBeGreaterThan(20)
        }
  })
})
