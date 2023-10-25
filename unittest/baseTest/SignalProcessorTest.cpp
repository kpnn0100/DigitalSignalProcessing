
#include <gtest/gtest.h>

#define private public
#define protected public
#include "../../base/SignalProcessor.h"
class ConcreteSignalProcessor : public SignalProcessor
{
public:
    ConcreteSignalProcessor() : SignalProcessor(1)
    {
        
    }
    double process(double in)
    {

    }
};
class SignalProcessorTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Initialize any common resources or configurations before each test.
    }

    void TearDown() override {
        // Clean up after each test.
    }
};


TEST_F(SignalProcessorTest, PropertyManipulationWithSmootherTest) {
    //without smoot
    ConcreteSignalProcessor processor;
    const int propertyId = 0;
    const double propertyValue = 42.0;

    // Test setting and getting properties
    processor.initProperty(propertyId, propertyValue);
    EXPECT_EQ(processor.getProperty(propertyId), propertyValue);

    // Test setting property target
    const double newPropertyValue = 56.0;
    processor.setProperty(propertyId, newPropertyValue);
    EXPECT_EQ(processor.getProperty(propertyId), propertyValue);
    EXPECT_EQ(processor.getPropertyTargetValue(propertyId), newPropertyValue);

    // Add more property manipulation tests here
}

TEST_F(SignalProcessorTest, PropertyManipulationWithoutSmootherTest) {
    //without smoot
    ConcreteSignalProcessor processor;
    processor.setSmoothEnable(false);
    const int propertyId = 0;
    const double propertyValue = 42.0;

    // Test setting and getting properties
    processor.initProperty(propertyId, propertyValue);
    EXPECT_EQ(processor.getProperty(propertyId), propertyValue);

    // Test setting property target
    const double newPropertyValue = 56.0;
    processor.setProperty(propertyId, newPropertyValue);
    EXPECT_EQ(processor.getProperty(propertyId), newPropertyValue);
    EXPECT_EQ(processor.getPropertyTargetValue(propertyId), newPropertyValue);

    // Add more property manipulation tests here
}
TEST_F(SignalProcessorTest, BufferCounterTest) {
    ConcreteSignalProcessor processor;
    const int bufferSize = 64;

    // Test buffer counter
    processor.setBufferSize(bufferSize);
    for (int i = 0; i < bufferSize; i++) {
        EXPECT_TRUE(processor.updateBufferCounter());
    }
    EXPECT_FALSE(processor.updateBufferCounter());
}
