#include "TestRunner.h"
#include "../layouts/GridLayout.h"

using namespace rncv;

static LayoutAttributes makeMeasuredGridItem(
    const std::string& key,
    double x,
    double y,
    double width,
    double height) {
  LayoutAttributes a;
  a.key = key;
  a.frame = {x, y, width, height};
  a.sizingState = SizingState::Measured;
  return a;
}

TEST(GridLayout, HorizontalComputeSections_UsesStashedMeasuredSizes) {
  auto cache = std::make_shared<LayoutCache>();
  GridLayout layout(cache);

  cache->setAttributes(makeMeasuredGridItem("grid-0-0", 10.0, 8.0, 150.0, 150.0));
  cache->setAttributes(makeMeasuredGridItem("grid-0-1", 10.0, 166.0, 110.0, 120.0));
  cache->stashMeasuredSizes();
  cache->clear();

  GridLayoutParams p;
  p.itemCount = 2;
  p.columns = 2;
  p.columnSpacing = 8.0;
  p.rowSpacing = 4.0;
  p.rowHeight = 110.0;
  p.sectionInsetTop = 8.0;
  p.sectionInsetBottom = 8.0;
  p.sectionInsetLeft = 10.0;
  p.sectionInsetRight = 10.0;
  p.horizontal = true;
  p.estimatedCrossAxisHeight = 110.0;
  p.keys = {"grid-0-0", "grid-0-1"};

  layout.computeSections({p});
  cache->clearStash();

  auto first = cache->getAttributes("grid-0-0");
  auto second = cache->getAttributes("grid-0-1");
  ASSERT_TRUE(first.has_value());
  ASSERT_TRUE(second.has_value());

  EXPECT_NEAR(first->frame.width, 150.0, 0.1);
  EXPECT_NEAR(first->frame.height, 150.0, 0.1);
  EXPECT_TRUE(first->sizingState == SizingState::Measured);

  EXPECT_NEAR(second->frame.width, 110.0, 0.1);
  EXPECT_NEAR(second->frame.height, 120.0, 0.1);
  EXPECT_NEAR(second->frame.y, 166.0, 0.1);
  EXPECT_TRUE(second->sizingState == SizingState::Measured);

  const auto total = cache->getTotalContentSize();
  EXPECT_NEAR(total.height, 324.0, 0.1);
}

TEST(GridLayout, HorizontalApplyMeasurements_UsesExplicitAxisForAmbiguousSquareFrames) {
  auto cache = std::make_shared<LayoutCache>();
  GridLayout layout(cache);

  GridLayoutParams p;
  p.itemCount = 1;
  p.columns = 1;
  p.rowSpacing = 4.0;
  p.sectionInsetTop = 8.0;
  p.sectionInsetBottom = 8.0;
  p.sectionInsetLeft = 10.0;
  p.sectionInsetRight = 10.0;
  p.horizontal = true;
  p.estimatedCrossAxisHeight = 102.0;
  p.keys = {"grid-0-0"};

  layout.computeSections({p});

  cache->setAttributes(makeMeasuredGridItem("grid-0-0", 10.0, 8.0, 102.0, 102.0));

  MeasurementDelta delta{
      "grid-0-0",
      0,
      102.0,
      110.0,
      MeasurementAxis::Width,
  };

  const bool handled = layout.applyMeasurements({delta}, *cache);
  EXPECT_TRUE(handled);

  auto first = cache->getAttributes("grid-0-0");
  ASSERT_TRUE(first.has_value());
  EXPECT_NEAR(first->frame.width, 110.0, 0.1);
  EXPECT_NEAR(first->frame.height, 102.0, 0.1);
}
